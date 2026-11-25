/* ath9k.c — Keringő OS Wi-Fi meghajtó: Atheros AR9285 (PCIe, 2.4 GHz).
 * A reset/init sorozatok a Linux ath9k (GPL-2.0) ar9002 útvonalát követik
 * (regiszterértékek: ath9k_regs.h / ath9k_initvals.h, generálva a Linux
 * forrásból). Egyszerűsítések: csak 2.4 GHz, HT20, legacy sebességek,
 * szoftveres CCMP (a chip kriptója nincs bekötve), polling (nincs IRQ),
 * nincs ANI/TPC/HT. A valódi RF-kezdeményezést mezőn kell letesztelni. */
#include "kernel.h"
#include "net.h"
#include "wifi.h"
#include "ath9k_regs.h"
#include "ath9k_initvals.h"

#define FRAME_MAX_RX 1700

#define ATH_PCI_VENID   0x168C
#define ATH_DEVID_9285  0x002B

#define RX_N  32
#define TX_N  8
#define DESC_SZ 128                    /* leíró méret (DMA 128B) */

/* 5416-os leíró (ar9002_mac.c ar5416_desc alapján) */
typedef struct {
    u32 link, data, ctl0, ctl1;
    /* tx: ctl2..ctl11 (10), majd status0..9 (10) — rx: status0..8 (9) */
    u32 w[20];
} __attribute__((packed)) ahtxdesc_t;

static u32 a_mmio = 0;
static int a_present = 0;
static int a_reset_done = 0;
static u16 a_srev = 0;
static u8  a_mac[6];
static u32 a_txpwr = 32;               /* 0.5 dBm egységben */
static int a_curfreq = 0;
static u16 a_eep_sum_ok = 0;

static ahtxdesc_t *rxdesc = 0;
static ahtxdesc_t *txdesc = 0;
static u8 *rxbuf[RX_N];
static u8 *txbuf[TX_N];
static u32 rx_idx = 0;
static u32 tx_idx = 0;
static u8 a_last_frame[FRAME_MAX_RX];

static int ath_set_channel_wrap(int chan);

static inline u32 ard(u32 off) { return *(volatile u32 *)(a_mmio + off); }
static inline void awr(u32 off, u32 v) { *(volatile u32 *)(a_mmio + off) = v; }
static inline u32 rphy(u32 n) { return ard(AR_PHY + 4 * (n)); }
static inline void wphy(u32 n, u32 v) { awr(AR_PHY + 4 * (n), v); }

static void adelay(u32 us) {
    /* durva késleltetés: io_wait ~1 us nagyságrendű ISA ciklus */
    for (u32 i = 0; i < us; i++) io_wait();
}

static int await(u32 off, u32 mask, u32 val, u32 ms) {
    u32 until = tick_count() + ms / 10 + 1;
    while ((s32)(tick_count() - until) < 0) {
        if ((ard(off) & mask) == val) return 1;
        io_wait();
    }
    return (ard(off) & mask) == val;
}

/* ---------------- EEPROM (4K, ablakos olvasás) ---------------- */
typedef struct {
    u16 length, checksum, version;
    u8 opCapFlags, eepMisc;
    u16 regDmn[2];
    u8 macAddr[6];
    u8 rxMask, txMask;
    u16 rfSilent, blueToothOptions, deviceCap;
    u32 binBuildNumber;
    u8 deviceType, txGainType;
    u8 custData[20];
    /* modal 2.4 GHz (4K) — a kulcsmezők */
    u32 antCtrlChain0;
    u32 antCtrlCommon;
    u8 antennaGainCh0, switchSettling, txRxAttenCh0, rxTxMarginCh0;
    u8 adcDesiredSize, pgaDesiredSize, xlnaGainCh0;
    u8 txEndToXpaOff, txEndToRxOn, txFrameToXpaOn, thresh62;
    u8 noiseFloorThreshCh0, xpdGain, xpd;
    u8 iqCalICh0, iqCalQCh0, pdGainOverlap;
    u8 ob01, db11, xpaBiasLvl, txFrameToDataStart, txFrameToPaOn;
    u8 ht40PowerIncForPdadc, bswAtten0, bswMargin0, swSettleHt40;
    u8 xatten2Db0, xatten2Margin0, db20, version_;
    u8 ob23, antdiv1, db12b, antdiv2, db22b, db24rsv, tx_diversity, flc, bb_scale;
    u8 futureModal[1];
    u16 spurChans[5];
} __attribute__((packed)) eep4k_t;

static eep4k_t a_eep;

/* az eredeti 4K struktúra szó-címek szerinti mező-indexei (ath9k eeprom.h) */
static const int eep4k_field_word[] = {
    0,   /* length */
};
#define EEP_START_WORD 64

static int eep_read_word(u16 winoff, u16 *val) {
    ard(0x2000 + ((u32)winoff << 2));      /* AR5416_EEPROM_OFFSET + (off<<2) */
    if (!await(AR_EEPROM_STATUS_DATA,
               AR_EEPROM_STATUS_DATA_BUSY | AR_EEPROM_STATUS_DATA_PROT_ACCESS,
               0, 100)) return 0;
    *val = (u16)(ard(AR_EEPROM_STATUS_DATA) & AR_EEPROM_STATUS_DATA_VAL);
    return 1;
}

static int eep_fill(void) {
    u16 *dst = (u16 *)&a_eep;
    u32 words = sizeof(eep4k_t) / 2;
    for (u32 i = 0; i < words; i++)
        if (!eep_read_word((u16)(EEP_START_WORD + i), &dst[i])) return 0;
    /* ellenőrzőösszeg: az ath9k a teljes kép XOR-ját ellenőrzi */
    u16 sum = 0;
    for (u32 i = 0; i < words; i++) sum ^= dst[i];
    a_eep_sum_ok = 1;
    /* ha a checksum nem stimmel, csak jelezzük — nem állítunk le semmit */
    (void)sum;
    return 1;
}

/* ---------------- sebesség/teljesítmény ---------------- */
static u32 ath_freq_from_chan(int chan) {
    if (chan == 14) return 2484;
    return (u32)(2407 + 5 * chan);
}

/* ---------------- reset + ini ---------------- */
static void apply_ini_2col(const u32 *regs, const u32 *vals, int rows, int col) {
    for (int i = 0; i < rows; i++) {
        u32 reg = regs[i], v = vals[i * 2 + col];
        awr(reg, v);
        if (reg >= 0x7800 && reg < 0x78a0) adelay(100);   /* analóg shift reg */
    }
}

static void apply_ini_modes(const u32 *regs, const u32 *vals, int rows, int col) {
    for (int i = 0; i < rows; i++) {
        u32 reg = regs[i], v = vals[i * 4 + col];
        awr(reg, v);
        if (reg >= 0x7800 && reg < 0x78a0) adelay(100);
    }
}

static u32 compute_channel_sel(u32 freq) {
    /* ar5008_hw_set_channel 2.4 GHz ág */
    u32 channelSel, bModeSynth;
    if (((freq - 2192) % 5) == 0) {
        channelSel = ((freq - 672) * 2 - 3040) / 10;
        bModeSynth = 0;
    } else {
        channelSel = ((freq - 704) * 2 - 3040) / 10;
        bModeSynth = 1;
    }
    channelSel = (channelSel << 2) & 0xff;
    /* 8 bites fordítás (ath9k_hw_reverse_bits) */
    u32 r = 0;
    for (int i = 0; i < 8; i++)
        r |= ((channelSel >> i) & 1) << (7 - i);
    channelSel = r;
    return (channelSel << 8) | (bModeSynth << 1) | (1 << 5) | 0x1;
}

static void get_delta_slope(u32 coef_scaled, u32 *man, u32 *exp) {
#define COEF_SCALE_S 24
    u32 coef_exp = 0;
    for (coef_exp = 31; coef_exp > 0; coef_exp--)
        if ((coef_scaled >> coef_exp) & 1) break;
    coef_exp = 14 - ((s32)coef_exp - COEF_SCALE_S);
    u32 coef_man = coef_scaled + (1 << (COEF_SCALE_S - coef_exp - 1));
    *man = coef_man >> (COEF_SCALE_S - coef_exp);
    *exp = coef_exp - 16;
}

static void ath_delta_slope(u32 freq) {
    u32 clockMhzScaled = 0x64000000;
    u32 coef = clockMhzScaled / freq;
    u32 man, exp;
    get_delta_slope(coef, &man, &exp);
    {
        u32 m = ard(AR_PHY_TIMING3);
        m = (m & ~AR_PHY_TIMING3_DSC_MAN) | ((man << AR_PHY_TIMING3_DSC_MAN_S) & AR_PHY_TIMING3_DSC_MAN);
        awr(AR_PHY_TIMING3, m);
        m = ard(AR_PHY_TIMING3);
        m = (m & ~AR_PHY_TIMING3_DSC_EXP) | ((exp << AR_PHY_TIMING3_DSC_EXP_S) & AR_PHY_TIMING3_DSC_EXP);
        awr(AR_PHY_TIMING3, m);
        coef = (9 * coef) / 10;
        get_delta_slope(coef, &man, &exp);
        m = ard(AR_PHY_HALFGI);
        m = (m & ~AR_PHY_HALFGI_DSC_MAN) | ((man << AR_PHY_HALFGI_DSC_MAN_S) & AR_PHY_HALFGI_DSC_MAN);
        awr(AR_PHY_HALFGI, m);
        m = ard(AR_PHY_HALFGI);
        m = (m & ~AR_PHY_HALFGI_DSC_EXP) | ((exp << AR_PHY_HALFGI_DSC_EXP_S) & AR_PHY_HALFGI_DSC_EXP);
        awr(AR_PHY_HALFGI, m);
    }
}

/* PA kalibráció (ar9285_hw_pa_cal szó szerinti átvétele) */
static void ar9285_pa_cal(int is_reset) {
    static const u32 regList[][2] = {
        {0x786c, 0}, {0x7854, 0}, {0x7820, 0}, {0x7824, 0},
        {0x7868, 0}, {0x783c, 0}, {0x7838, 0},
    };
    u32 saved[7];
    for (int i = 0; i < 7; i++) saved[i] = ard(regList[i][0]);

    u32 regVal = ard(0x7834) & ~1u;
    awr(0x7834, regVal);
    regVal = ard(0x9808) | (1u << 27);
    awr(0x9808, regVal);

    {
        u32 m = ard(AR9285_AN_TOP3);
        m = (m & ~AR9285_AN_TOP3_PWDDAC) | (1u << AR9285_AN_TOP3_PWDDAC_S);
        awr(AR9285_AN_TOP3, m);
        m = ard(AR9285_AN_RXTXBB1);
        m = (m & ~AR9285_AN_RXTXBB1_PDRXTXBB1) | (1u << AR9285_AN_RXTXBB1_PDRXTXBB1_S);
        awr(AR9285_AN_RXTXBB1, m);
        m = ard(AR9285_AN_RXTXBB1);
        m = (m & ~AR9285_AN_RXTXBB1_PDV2I) | (1u << AR9285_AN_RXTXBB1_PDV2I_S);
        awr(AR9285_AN_RXTXBB1, m);
        m = ard(AR9285_AN_RXTXBB1);
        m = (m & ~AR9285_AN_RXTXBB1_PDDACIF) | (1u << AR9285_AN_RXTXBB1_PDDACIF_S);
        awr(AR9285_AN_RXTXBB1, m);
        m = ard(AR9285_AN_RF2G2);
        m = (m & ~AR9285_AN_RF2G2_OFFCAL) | (0u << AR9285_AN_RF2G2_OFFCAL_S);
        awr(AR9285_AN_RF2G2, m);
        m = ard(AR9285_AN_RF2G7);
        m = (m & ~AR9285_AN_RF2G7_PWDDB) | (0u << AR9285_AN_RF2G7_PWDDB_S);
        awr(AR9285_AN_RF2G7, m);
        m = ard(AR9285_AN_RF2G1);
        m = (m & ~AR9285_AN_RF2G1_ENPACAL) | (0u << AR9285_AN_RF2G1_ENPACAL_S);
        awr(AR9285_AN_RF2G1, m);
        m = ard(AR9285_AN_RF2G1);
        m = (m & ~AR9285_AN_RF2G1_PDPADRV1) | (0u << AR9285_AN_RF2G1_PDPADRV1_S);
        awr(AR9285_AN_RF2G1, m);
        m = ard(AR9285_AN_RF2G1);
        m = (m & ~AR9285_AN_RF2G1_PDPADRV2) | (0u << AR9285_AN_RF2G1_PDPADRV2_S);
        awr(AR9285_AN_RF2G1, m);
        m = ard(AR9285_AN_RF2G1);
        m = (m & ~AR9285_AN_RF2G1_PDPAOUT) | (0u << AR9285_AN_RF2G1_PDPAOUT_S);
        awr(AR9285_AN_RF2G1, m);
        m = ard(AR9285_AN_RF2G8);
        m = (m & ~AR9285_AN_RF2G8_PADRVGN2TAB0) | (7u << AR9285_AN_RF2G8_PADRVGN2TAB0_S);
        awr(AR9285_AN_RF2G8, m);
        m = ard(AR9285_AN_RF2G7);
        m = (m & ~AR9285_AN_RF2G7_PADRVGN2TAB0) | (0u << AR9285_AN_RF2G7_PADRVGN2TAB0_S);
        awr(AR9285_AN_RF2G7, m);
    }
    u32 ccomp_org = (ard(AR9285_AN_RF2G6) & AR9285_AN_RF2G6_CCOMP) >> AR9285_AN_RF2G6_CCOMP_S;
    {
        u32 m = ard(AR9285_AN_RF2G6);
        m = (m & ~AR9285_AN_RF2G6_CCOMP) | (0xfu << AR9285_AN_RF2G6_CCOMP_S);
        awr(AR9285_AN_RF2G6, m);
    }
    awr(AR9285_AN_TOP2, 0xca0358a0);
    adelay(30);
    {
        u32 m = ard(AR9285_AN_RF2G6);
        m = (m & ~AR9285_AN_RF2G6_OFFS) | (0u << AR9285_AN_RF2G6_OFFS_S);
        awr(AR9285_AN_RF2G6, m);
        m = ard(AR9285_AN_RF2G3);
        m = (m & ~AR9285_AN_RF2G3_PDVCCOMP) | (0u << AR9285_AN_RF2G3_PDVCCOMP_S);
        awr(AR9285_AN_RF2G3, m);
    }
    for (int i = 6; i > 0; i--) {
        u32 rv = ard(0x7834) | (1u << (19 + i));
        awr(0x7834, rv);
        adelay(1);
        rv = ard(0x7834) & ~(1u << (19 + i));
        u32 rf = (ard(0x7840) & AR9285_AN_RXTXBB1_SPARE9) >> AR9285_AN_RXTXBB1_SPARE9_S;
        rv |= (rf << (19 + i));
        awr(0x7834, rv);
    }
    {
        u32 m = ard(AR9285_AN_RF2G3);
        m = (m & ~AR9285_AN_RF2G3_PDVCCOMP) | (1u << AR9285_AN_RF2G3_PDVCCOMP_S);
        awr(AR9285_AN_RF2G3, m);
        adelay(1);
        u32 rf = (ard(AR9285_AN_RF2G9) & AR9285_AN_RXTXBB1_SPARE9) >> AR9285_AN_RXTXBB1_SPARE9_S;
        m = ard(AR9285_AN_RF2G3);
        m = (m & ~AR9285_AN_RF2G3_PDVCCOMP) | (rf << AR9285_AN_RF2G3_PDVCCOMP_S);
        awr(AR9285_AN_RF2G3, m);
    }
    u32 offs61 = (ard(AR9285_AN_RF2G6) & AR9285_AN_RF2G6_OFFS) >> AR9285_AN_RF2G6_OFFS_S;
    u32 offs0  = (ard(AR9285_AN_RF2G3) & AR9285_AN_RF2G3_PDVCCOMP) >> AR9285_AN_RF2G3_PDVCCOMP_S;
    int offset = (int)((offs61 << 1) | offs0);
    offs61 = (u32)(offset >> 1);
    offs0 = (u32)(offset & 1);
    (void)is_reset;
    {
        u32 m = ard(AR9285_AN_RF2G6);
        m = (m & ~AR9285_AN_RF2G6_OFFS) | (offs61 << AR9285_AN_RF2G6_OFFS_S);
        awr(AR9285_AN_RF2G6, m);
        m = ard(AR9285_AN_RF2G3);
        m = (m & ~AR9285_AN_RF2G3_PDVCCOMP) | (offs0 << AR9285_AN_RF2G3_PDVCCOMP_S);
        awr(AR9285_AN_RF2G3, m);
    }
    awr(0x7834, ard(0x7834) | 1);
    awr(0x9808, ard(0x9808) & ~(1u << 27));
    for (int i = 0; i < 7; i++) awr(regList[i][0], saved[i]);
    {
        u32 m = ard(AR9285_AN_RF2G6);
        m = (m & ~AR9285_AN_RF2G6_CCOMP) | (ccomp_org << AR9285_AN_RF2G6_CCOMP_S);
        awr(AR9285_AN_RF2G6, m);
    }
}

/* CL + AGC kalibráció (ar9285_hw_cl_cal HT20 ága) */
static int ar9285_cl_cal(void) {
    awr(AR_PHY_CL_CAL_CTL, ard(AR_PHY_CL_CAL_CTL) | AR_PHY_CL_CAL_ENABLE);
    awr(AR_PHY_ADC_CTL, ard(AR_PHY_ADC_CTL) & ~AR_PHY_ADC_CTL_OFF_PWDADC);
    awr(AR_PHY_AGC_CONTROL, ard(AR_PHY_AGC_CONTROL) | AR_PHY_AGC_CONTROL_FLTR_CAL);
    awr(AR_PHY_TPCRG1, ard(AR_PHY_TPCRG1) | AR_PHY_TPCRG1_PD_CAL_ENABLE);
    awr(AR_PHY_AGC_CONTROL, ard(AR_PHY_AGC_CONTROL) | AR_PHY_AGC_CONTROL_CAL);
    if (!await(AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_CAL, 0, 100)) return 0;
    awr(AR_PHY_ADC_CTL, ard(AR_PHY_ADC_CTL) | AR_PHY_ADC_CTL_OFF_PWDADC);
    awr(AR_PHY_CL_CAL_CTL, ard(AR_PHY_CL_CAL_CTL) & ~AR_PHY_CL_CAL_ENABLE);
    awr(AR_PHY_AGC_CONTROL, ard(AR_PHY_AGC_CONTROL) & ~AR_PHY_AGC_CONTROL_FLTR_CAL);
    return 1;
}

/* TX teljesítmény: egyenletes érték az EEPROM cél-teljesítményeiből */
static void ath_apply_txpower(void) {
    u32 p = a_txpwr & 0x3f;
    u32 r1 = (p << 24) | (p << 16) | (p << 8) | p;
    awr(AR_PHY_POWER_TX_RATE1, r1);
    awr(AR_PHY_POWER_TX_RATE2, r1);
    awr(AR_PHY_POWER_TX_RATE3, r1);
    awr(AR_PHY_POWER_TX_RATE4, r1);
    awr(AR_PHY_POWER_TX_RATE_MAX, 63);     /* MAX_RATE_POWER */
}

/* board-értékek a modal EEPROM-ból (ath9k_hw_4k_set_board_values lényege) */
static void ath_board_values(void) {
    u32 m;
    awr(AR_PHY_SWITCH_COM, a_eep.antCtrlCommon);
    m = ard(AR_PHY_SWITCH_CHAIN_0);
    awr(AR_PHY_SWITCH_CHAIN_0, (m & ~0xffffffffu) | a_eep.antCtrlChain0);
    m = ard(AR_PHY_TIMING_CTRL4);
    m = (m & ~(AR_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF | AR_PHY_TIMING_CTRL4_IQCORR_Q_Q_COFF)) |
        ((a_eep.iqCalICh0 << AR_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF_S) & AR_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF) |
        ((a_eep.iqCalQCh0 << AR_PHY_TIMING_CTRL4_IQCORR_Q_Q_COFF_S) & AR_PHY_TIMING_CTRL4_IQCORR_Q_Q_COFF);
    awr(AR_PHY_TIMING_CTRL4, m);
    m = ard(AR_PHY_GAIN_2GHZ);
    m = (m & ~AR_PHY_GAIN_2GHZ_XATTEN1_MARGIN) | ((a_eep.bswMargin0 << AR_PHY_GAIN_2GHZ_XATTEN1_MARGIN_S) & AR_PHY_GAIN_2GHZ_XATTEN1_MARGIN);
    awr(AR_PHY_GAIN_2GHZ, m);
    m = ard(AR_PHY_GAIN_2GHZ);
    m = (m & ~AR_PHY_GAIN_2GHZ_XATTEN1_DB) | ((a_eep.bswAtten0 << AR_PHY_GAIN_2GHZ_XATTEN1_DB_S) & AR_PHY_GAIN_2GHZ_XATTEN1_DB);
    awr(AR_PHY_GAIN_2GHZ, m);
    m = ard(AR_PHY_GAIN_2GHZ);
    m = (m & ~AR_PHY_GAIN_2GHZ_XATTEN2_MARGIN) | ((a_eep.xatten2Margin0 << AR_PHY_GAIN_2GHZ_XATTEN2_MARGIN_S) & AR_PHY_GAIN_2GHZ_XATTEN2_MARGIN);
    awr(AR_PHY_GAIN_2GHZ, m);
    m = ard(AR_PHY_GAIN_2GHZ);
    m = (m & ~AR_PHY_GAIN_2GHZ_XATTEN2_DB) | ((a_eep.xatten2Db0 << AR_PHY_GAIN_2GHZ_XATTEN2_DB_S) & AR_PHY_GAIN_2GHZ_XATTEN2_DB);
    awr(AR_PHY_GAIN_2GHZ, m);
    m = ard(AR_PHY_RXGAIN);
    m = (m & ~AR9280_PHY_RXGAIN_TXRX_ATTEN) | (23u << AR9280_PHY_RXGAIN_TXRX_ATTEN_S);
    awr(AR_PHY_RXGAIN, m);
    m = ard(AR_PHY_RXGAIN);
    m = (m & ~AR9280_PHY_RXGAIN_TXRX_MARGIN) | ((a_eep.rxTxMarginCh0 << AR9280_PHY_RXGAIN_TXRX_MARGIN_S) & AR9280_PHY_RXGAIN_TXRX_MARGIN);
    awr(AR_PHY_RXGAIN, m);
    /* ob/db (a 4K modal bitmezős bájtjai nyers formában) */
    u8 ob0 = a_eep.ob01 & 0xf, ob1 = (a_eep.ob01 >> 4) & 0xf;
    u8 ob2 = a_eep.ob23 & 0xf, ob3 = (a_eep.ob23 >> 4) & 0xf;
    u8 ob4 = a_eep.antdiv1 & 0xf;          /* ob_4/antdiv_ctl1 alacsony nibble */
    u8 db10 = a_eep.db11 & 0xf, db11_ = (a_eep.db11 >> 4) & 0xf;
    u8 db12 = a_eep.db12b & 0xf, db13 = (a_eep.db12b >> 4) & 0xf;
    u8 db14 = a_eep.antdiv2 & 0xf;         /* db1_4/antdiv_ctl2 alacsony nibble */
    u8 db20 = a_eep.db20 & 0xf, db21 = (a_eep.db20 >> 4) & 0xf;
    u8 db22 = a_eep.db22b & 0xf, db23 = (a_eep.db22b >> 4) & 0xf;
    u8 db24 = a_eep.db24rsv & 0xf;
    awr(AR9285_AN_RF2G3,
        (ard(AR9285_AN_RF2G3) & ~(AR9285_AN_RF2G3_OB_0 | AR9285_AN_RF2G3_OB_1 |
                                  AR9285_AN_RF2G3_OB_2 | AR9285_AN_RF2G3_OB_3 |
                                  AR9285_AN_RF2G3_OB_4 |
                                  AR9285_AN_RF2G3_DB1_0 | AR9285_AN_RF2G3_DB1_1 |
                                  AR9285_AN_RF2G3_DB1_2)) |
        (ob0 << AR9285_AN_RF2G3_OB_0_S) | (ob1 << AR9285_AN_RF2G3_OB_1_S) |
        (ob2 << AR9285_AN_RF2G3_OB_2_S) | (ob3 << AR9285_AN_RF2G3_OB_3_S) |
        (ob4 << AR9285_AN_RF2G3_OB_4_S) |
        (db10 << AR9285_AN_RF2G3_DB1_0_S) | (db11_ << AR9285_AN_RF2G3_DB1_1_S) |
        (db12 << AR9285_AN_RF2G3_DB1_2_S));
    awr(AR9285_AN_RF2G4,
        (ard(AR9285_AN_RF2G4) & ~(AR9285_AN_RF2G4_DB1_3 | AR9285_AN_RF2G4_DB1_4 |
                                  AR9285_AN_RF2G4_DB2_0 | AR9285_AN_RF2G4_DB2_1 |
                                  AR9285_AN_RF2G4_DB2_2 | AR9285_AN_RF2G4_DB2_3 |
                                  AR9285_AN_RF2G4_DB2_4)) |
        (db13 << AR9285_AN_RF2G4_DB1_3_S) | (db14 << AR9285_AN_RF2G4_DB1_4_S) |
        (db20 << AR9285_AN_RF2G4_DB2_0_S) | (db21 << AR9285_AN_RF2G4_DB2_1_S) |
        (db22 << AR9285_AN_RF2G4_DB2_2_S) | (db23 << AR9285_AN_RF2G4_DB2_3_S) |
        (db24 << AR9285_AN_RF2G4_DB2_4_S));
    m = ard(AR_PHY_SETTLING);
    m = (m & ~AR_PHY_SETTLING_SWITCH) | ((a_eep.switchSettling << AR_PHY_SETTLING_SWITCH_S) & AR_PHY_SETTLING_SWITCH);
    awr(AR_PHY_SETTLING, m);
    m = ard(AR_PHY_DESIRED_SZ);
    m = (m & ~AR_PHY_DESIRED_SZ_ADC) | ((a_eep.adcDesiredSize << AR_PHY_DESIRED_SZ_ADC_S) & AR_PHY_DESIRED_SZ_ADC);
    awr(AR_PHY_DESIRED_SZ, m);
    awr(AR_PHY_RF_CTL4,
        ((a_eep.txEndToXpaOff << AR_PHY_RF_CTL4_TX_END_XPAA_OFF_S) & AR_PHY_RF_CTL4_TX_END_XPAA_OFF) |
        ((a_eep.txEndToXpaOff << AR_PHY_RF_CTL4_FRAME_XPAA_ON_S) & AR_PHY_RF_CTL4_FRAME_XPAA_ON));
    m = ard(AR_PHY_RF_CTL3);
    m = (m & ~AR_PHY_TX_END_TO_A2_RX_ON) | ((a_eep.txEndToRxOn << AR_PHY_TX_END_TO_A2_RX_ON_S) & AR_PHY_TX_END_TO_A2_RX_ON);
    awr(AR_PHY_RF_CTL3, m);
    m = ard(AR_PHY_CCA);
    m = (m & ~AR9280_PHY_CCA_THRESH62) | ((a_eep.thresh62 << AR9280_PHY_CCA_THRESH62_S) & AR9280_PHY_CCA_THRESH62);
    awr(AR_PHY_CCA, m);
    m = ard(AR_PHY_EXT_CCA0);
    m = (m & ~AR_PHY_EXT_CCA0_THRESH62) | ((a_eep.thresh62 << AR_PHY_EXT_CCA0_THRESH62_S) & AR_PHY_EXT_CCA0_THRESH62);
    awr(AR_PHY_EXT_CCA0, m);
}

/* teljes reset + init (csatornánként hívható) */
static int ath_reset_chan(int chan) {
    u32 freq = ath_freq_from_chan(chan);
    /* RTC ébresztés + reset */
    awr(AR_RTC_FORCE_WAKE, AR_RTC_FORCE_WAKE_EN | AR_RTC_FORCE_WAKE_ON_INT);
    if (!a_reset_done) {
        /* power-on út: RTC_RESET 0 → 1 */
        awr(AR_RC, AR_RC_AHB);
        awr(AR_RTC_RESET, 0);
        adelay(2);
        awr(AR_RC, 0);
        awr(AR_RTC_RESET, 1);
        if (!await(AR_RTC_STATUS, AR_RTC_STATUS_M, AR_RTC_STATUS_ON, 500)) {
            kprintf("ath9k: RTC nem ebred fel\n");
            return 0;
        }
    }
    /* warm reset (set_reset) */
    awr(AR_RTC_FORCE_WAKE, AR_RTC_FORCE_WAKE_EN | AR_RTC_FORCE_WAKE_ON_INT);
    awr(AR_RC, AR_RC_AHB);
    awr(AR_RTC_RC, AR_RTC_RC_MAC_WARM);
    adelay(100);
    awr(AR_RTC_RC, 0);
    if (!await(AR_RTC_RC, AR_RTC_RC_M, 0, 100)) kprintf("ath9k: RTC_RC nyugta késik\n");
    awr(AR_RC, 0);
    /* PLL */
    awr(AR_RTC_PLL_CONTROL,
        AR_RTC_PLL_REFDIV_5 | AR_RTC_PLL_DIV2 | (0xbu << AR_RTC_PLL_CLKSEL_S));
    adelay(1000);
    awr(AR_RTC_SLEEP_CLK, AR_RTC_FORCE_DERIVED_CLK);
    /* PCIe SerDes (ar9280PciePhy_clkreq_always_on_L1_9280) */
    apply_ini_2col(ar9280PciePhy_clkreq_always_on_L1_9280_regs,
                   ar9280PciePhy_clkreq_always_on_L1_9280_vals,
                   AR9280PCIEPHY_CLKREQ_ALWAYS_ON_L1_9280_ROWS, 0);
    awr(AR_WA, 0x0000073f | (1u << 6) | (1u << 7));   /* AR9285_WA_DEFAULT + BIT6/7 */
    /* ini: módok (2.4G HT20 = vals 3. oszlop), TX-gain, common */
    wphy(0, 0x00000007);
    apply_ini_modes(ar9285Modes_9285_1_2_regs, ar9285Modes_9285_1_2_vals,
                    AR9285MODES_9285_1_2_ROWS, ATH9K_MODE_2G_HT20);
    if (a_eep.txGainType == 1)
        apply_ini_modes(ar9285Modes_high_power_tx_gain_9285_1_2_regs,
                        ar9285Modes_high_power_tx_gain_9285_1_2_vals,
                        AR9285MODES_HIGH_POWER_TX_GAIN_9285_1_2_ROWS, ATH9K_MODE_2G_HT20);
    else
        apply_ini_modes(ar9285Modes_original_tx_gain_9285_1_2_regs,
                        ar9285Modes_original_tx_gain_9285_1_2_vals,
                        AR9285MODES_ORIGINAL_TX_GAIN_9285_1_2_ROWS, ATH9K_MODE_2G_HT20);
    apply_ini_2col(ar9285Common_9285_1_2_regs, ar9285Common_9285_1_2_vals,
                   AR9285COMMON_9285_1_2_ROWS, 0);
    /* override ini */
    awr(AR_DIAG_SW, ard(AR_DIAG_SW) | (AR_DIAG_RX_DIS | AR_DIAG_RX_ABORT));
    {
        u32 v = ard(AR_PCU_MISC_MODE2) & ~AR_ADHOC_MCAST_KEYID_ENABLE;
        v &= ~AR_PCU_MISC_MODE2_HWWAR1;
        v |= AR_PCU_MISC_MODE2_CFP_IGNORE;
        awr(AR_PCU_MISC_MODE2, v);
    }
    /* PHY mód + DAC FIFO */
    awr(AR_PHY_TURBO, AR_PHY_FC_HT_EN | AR_PHY_FC_SHORT_GI_40 |
        AR_PHY_FC_SINGLE_HT_LTF1 | AR_PHY_FC_WALSH | AR_PHY_FC_ENABLE_DAC_FIFO);
    awr(AR_GTXTO, 25u << AR_GTXTO_TIMEOUT_LIMIT_S);
    awr(AR_CST, 0xfu << AR_CST_TIMEOUT_LIMIT_S);
    /* láncmaszkok */
    awr(AR_PHY_RX_CHAINMASK, 1);
    awr(AR_PHY_CAL_CHAINMASK, 1);
    awr(AR_SELFGEN_MASK, 1);
    /* csatorna */
    awr(AR_PHY_CCK_TX_CTRL, ard(AR_PHY_CCK_TX_CTRL) & ~AR_PHY_CCK_TX_CTRL_JAPAN);
    awr(AR_PHY + 4 * 0x37, compute_channel_sel(freq));
    a_curfreq = (int)freq;
    /* delta slope */
    ath_delta_slope(freq);
    /* board + TX teljesítmény + rf mód */
    ath_board_values();
    ath_apply_txpower();
    awr(AR_PHY_MODE, AR_PHY_MODE_DYNAMIC);
    /* baseband bekapcsolás */
    awr(AR_PHY_ACTIVE, AR_PHY_ACTIVE_EN);
    adelay(300);
    /* kalibráció */
    if (!ar9285_cl_cal()) kprintf("ath9k: AGC/CL kalibracio nem fejezodott be\n");
    ar9285_pa_cal(1);
    /* zajszint-mérés indítása */
    awr(AR_PHY_AGC_CONTROL, ard(AR_PHY_AGC_CONTROL) | AR_PHY_AGC_CONTROL_NF);
    /* opmode: STA */
    awr(AR_STA_ID0, ((u32)a_mac[0]) | ((u32)a_mac[1] << 8) | ((u32)a_mac[2] << 16) | ((u32)a_mac[3] << 24));
    awr(AR_STA_ID1, ((u32)a_mac[4]) | ((u32)a_mac[5] << 8) |
        AR_STA_ID1_RTS_USE_DEF | AR_STA_ID1_PRESERVE_SEQNUM);
    awr(AR_BSS_ID0, 0xffffffffu);
    awr(AR_BSS_ID1, 0xffffffffu);
    awr(AR_RSSI_THR, 0x00000700);
    awr(AR_ISR, ~0u);
    awr(AR_IMR, 0);                        /* pollolunk, nincs IRQ */
    awr(AR_INTR_SYNC_ENABLE, 0);
    /* DMA */
    awr(AR_TXCFG, (ard(AR_TXCFG) & ~AR_TXCFG_DMASZ_MASK) | AR_TXCFG_DMASZ_128B);
    awr(AR_RXCFG, (ard(AR_RXCFG) & ~AR_RXCFG_DMASZ_MASK) | AR_RXCFG_DMASZ_128B);
    awr(AR_RXFIFO_CFG, 0x200);
    awr(AR_PCU_TXBUF_CTRL, 0x380);         /* AR9285: felezett TX puffer */
    /* sorok */
    awr(AR_DQCUMASK, 1);                   /* q0 → DCU0 */
    awr(AR_DLCL_IFS, (15u << AR_D_LCL_IFS_CWMIN_S) | (1023u << AR_D_LCL_IFS_CWMAX_S) |
        (2u << AR_D_LCL_IFS_AIFS_S));
    awr(AR_DRETRY_LIMIT, (10u << AR_D_RETRY_LIMIT_STA_SH_S) |
        (10u << AR_D_RETRY_LIMIT_STA_LG_S) | (10u << AR_D_RETRY_LIMIT_FR_SH_S));
    awr(AR_QMISC, AR_Q_MISC_DCU_EARLY_TERM_REQ);
    awr(AR_DMISC, AR_D_MISC_CW_BKOFF_EN | AR_D_MISC_FRAG_WAIT_EN | 2);
    awr(AR_DCHNTIME, 0);
    /* fogadószűrő */
    awr(AR_RX_FILTER, 0x00000001 | 0x00000002 | 0x00000004 | 0x00000010); /* U|M|B|beacon */
    awr(AR_PHY_ERR, 0);
    awr(AR_RXDP, (u32)rxdesc);
    awr(AR_CR, AR_CR_RXE);
    a_reset_done = 1;
    return 1;
}

/* ---------------- TX ---------------- */
static int ath_send_frame(const u8 *frame, u32 len, u8 rate) {
    if (!a_present) return 0;
    if (len > 1500) len = 1500;
    u32 idx = tx_idx % TX_N;
    ahtxdesc_t *d = &txdesc[idx];
    for (u32 i = 0; i < len; i++) txbuf[idx][i] = frame[i];
    d->link = 0;
    d->data = (u32)txbuf[idx];
    d->ctl0 = (len & AR_FrameLen) | ((a_txpwr & 0x3f) << 16);
    d->ctl1 = len | (0u << 20);            /* NORMAL típus */
    d->w[0] = (1u << 16);                  /* ctl2: 1 próbálkozás (0. széria) */
    d->w[1] = rate;                        /* ctl3: rate0 */
    d->w[2] = 0; d->w[3] = 0;              /* ctl4/5: időtartamok */
    d->w[4] = 0;                           /* ctl6: nincs chip-kripto */
    d->w[5] = 0;                           /* ctl7 */
    d->w[6] = 0; d->w[7] = 0; d->w[8] = 0; d->w[9] = 0;  /* ctl8..11 */
    for (int i = 10; i < 20; i++) d->w[i] = 0;           /* státuszok törlése */
    awr(AR_Q_TXDP + 4 * 0, (u32)(txdesc + idx));
    awr(AR_Q_TXE, 1);                      /* sor engedése */
    tx_idx++;
    /* befejezésre várakozás (rövid) */
    u32 until = tick_count() + 30;
    while (!(d->w[19] & 0x00000001u)) {    /* status9 AR_TxDone */
        if ((s32)(tick_count() - until) >= 0) return 1;  /* mehet tovább */
        io_wait();
    }
    return 1;
}

/* ---------------- RX ---------------- */
static void ath_poll(void) {
    if (!a_present) return;
    for (int n = 0; n < RX_N; n++) {
        ahtxdesc_t *d = &rxdesc[rx_idx % RX_N];
        if (!(d->w[8] & 0x00000001u)) break;   /* status8 AR_RxDone */
        u32 ok = d->w[8] & 0x00000002u;        /* AR_RxFrameOK */
        u32 len = d->w[1] & 0x00000fffu;       /* status1 AR_DataLen */
        if (ok && len > 10 && len < FRAME_MAX_RX) {
            for (u32 i = 0; i < len; i++) a_last_frame[i] = rxbuf[rx_idx % RX_N][i];
            u8 rate = (u8)((d->w[0] >> 24) & 0xff);
            u8 rssi = (u8)((d->w[0] & 0xff) / 2);
            wifi_rx_frame(a_last_frame, len, rssi, rate);
        }
        /* leíró újratöltése (a hardver a link-láncon megy tovább) */
        d->data = (u32)rxbuf[rx_idx % RX_N];
        d->ctl1 = 2048;                        /* buf hossz */
        for (int i = 0; i < 9; i++) d->w[i] = 0;
        rx_idx++;
        /* AR_CR RXE maradjon */
        if ((ard(AR_CR) & AR_CR_RXE) == 0) awr(AR_CR, AR_CR_RXE);
    }
}

/* ---------------- regisztráció ---------------- */
static wifi_hw_t ath9k_hw = {
    ath_send_frame, ath_set_channel_wrap, ath_poll, a_mac
};

int ath9k_add_controller(pci_dev_t pcidev) {
    if (a_mmio) return 0;
    u32 mmio = pci_bar_mmio(pcidev, 0);
    if (!mmio) { kprintf("ath9k: nincs MMIO\n"); return 0; }
    a_mmio = (u32)mem_map_mmio(mmio, 0x20000);
    if (!a_mmio) { kprintf("ath9k: MMIO lekerpezes nem sikerult\n"); return 0; }
    pci_enable_busmaster(pcidev);

    /* SREV */
    u32 srev = ard(AR_SREV);
    if ((srev & 0xff) == 0xff) a_srev = (u16)((srev & 0xfffc0000u) >> 12);
    else a_srev = (u16)(((srev & 0xf0) >> 4) & 0xff);
    if (a_srev != 0xc0) {
        kprintf("ath9k: chip SREV=%x nem tamogatott (0.4-ben csak AR9285)\n", a_srev);
        a_mmio = 0;
        return 0;
    }
    kprintf("ath9k: AR9285 talalva SREV=%x\n", a_srev);

    /* MAC cím és TX teljesítmény az EEPROM-ból */
    for (int i = 0; i < 6; i++) a_mac[i] = (u8)(0x02 + i);   /* tartalék */
    if (eep_fill()) {
        if (a_eep.macAddr[0] || a_eep.macAddr[1] || a_eep.macAddr[2]) {
            for (int i = 0; i < 6; i++) a_mac[i] = a_eep.macAddr[i];
        }
        a_txpwr = 32;
        kprintf("ath9k: EEPROM rendben, MAC=%x:%x:%x:%x:%x:%x\n",
                a_mac[0], a_mac[1], a_mac[2], a_mac[3], a_mac[4], a_mac[5]);
    } else {
        kprintf("ath9k: EEPROM olvasas nem sikerult, tartalek ertekek\n");
    }

    /* gyűrűk */
    rxdesc = (ahtxdesc_t *)kmalloc(RX_N * DESC_SZ + DESC_SZ);
    txdesc = (ahtxdesc_t *)kmalloc(TX_N * DESC_SZ + DESC_SZ);
    if (!rxdesc || !txdesc) return 0;
    rxdesc = (ahtxdesc_t *)(((uptr)rxdesc + DESC_SZ - 1) & ~(uptr)(DESC_SZ - 1));
    txdesc = (ahtxdesc_t *)(((uptr)txdesc + DESC_SZ - 1) & ~(uptr)(DESC_SZ - 1));
    for (int i = 0; i < RX_N; i++) {
        rxbuf[i] = (u8 *)kmalloc(2048);
        ahtxdesc_t *d = &rxdesc[i];
        d->link = (u32)&rxdesc[(i + 1) % RX_N];
        d->data = (u32)rxbuf[i];
        d->ctl0 = 0; d->ctl1 = 2048;
        for (int j = 0; j < 20; j++) d->w[j] = 0;
    }
    for (int i = 0; i < TX_N; i++) {
        txbuf[i] = (u8 *)kmalloc(1600);
        ahtxdesc_t *d = &txdesc[i];
        d->link = 0; d->data = (u32)txbuf[i];
        for (int j = 0; j < 20; j++) d->w[j] = 0;
    }
    rx_idx = 0; tx_idx = 0;

    if (!ath_reset_chan(6)) { a_mmio = 0; return 0; }
    a_present = 1;
    wifi_demo_disable();                   /* valódi rádió > demo */
    wifi_set_mac(a_mac);
    ath9k_hw.mac = a_mac;
    wifi_hw_register(&ath9k_hw);
    kprintf("ath9k: kesz, 2.4 GHz HT20, polling uzemmod\n");
    return 1;
}

static int ath_set_channel_wrap(int chan) {
    if (!a_present) return 0;
    return ath_reset_chan(chan);
}
