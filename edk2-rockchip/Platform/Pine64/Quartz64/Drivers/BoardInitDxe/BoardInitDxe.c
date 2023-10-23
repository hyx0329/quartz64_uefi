/** @file
 *
 *  Board init for the Quartz64 platform
 *
 *  Copyright (c) 2021, Jared McNeill <jmcneill@invisible.ca>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Base.h>
#include <Library/ArmLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiLib.h>
#include <Library/BaseLib.h>
#include <Library/PcdLib.h>
#include <Library/PrintLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/CruLib.h>
#include <Library/GpioLib.h>
#include <Library/I2cLib.h>
#include <Library/MultiPhyLib.h>
#include <Library/OtpLib.h>
#include <Library/SocLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseCryptLib.h>

#include <IndustryStandard/Rk356x.h>
#include <IndustryStandard/Rk356xCru.h>
#include <ConfigVars.h>

#include "EthernetPhy.h"

/*
 * GMAC registers
 */
#define GMAC1_MAC_ADDRESS0_LOW  (GMAC1_BASE + 0x0304)
#define GMAC1_MAC_ADDRESS0_HIGH (GMAC1_BASE + 0x0300)

#define GRF_MAC1_CON0           (SYS_GRF + 0x0388)
#define  CLK_RX_DL_CFG_SHIFT    8
#define  CLK_TX_DL_CFG_SHIFT    0
#define GRF_MAC1_CON1           (SYS_GRF + 0x038C)
#define  PHY_INTF_SEL_SHIFT     4
#define  PHY_INTF_SEL_MASK      (0x7U << PHY_INTF_SEL_SHIFT)
#define  PHY_INTF_SEL_RGMII     (1U << PHY_INTF_SEL_SHIFT)
#define  FLOWCTRL               BIT3
#define  MAC_SPEED              BIT2
#define  RXCLK_DLY_ENA          BIT1
#define  TXCLK_DLY_ENA          BIT0

#define TX_DELAY                0x30
#define RX_DELAY                0x10

/*
 * PMIC registers
*/
#define PMIC_I2C_ADDR           0x20

#define PMIC_CHIP_NAME          0xed
#define PMIC_CHIP_VER           0xee
#define PMIC_POWER_EN1          0xb2
#define PMIC_POWER_EN2          0xb3
#define PMIC_POWER_EN3          0xb4
#define PMIC_LDO1_ON_VSEL       0xcc
#define PMIC_LDO2_ON_VSEL       0xce
#define PMIC_LDO3_ON_VSEL       0xd0
#define PMIC_LDO4_ON_VSEL       0xd2
#define PMIC_LDO6_ON_VSEL       0xd6
#define PMIC_LDO7_ON_VSEL       0xd8
#define PMIC_LDO8_ON_VSEL       0xda
#define PMIC_LDO9_ON_VSEL       0xdc

/*
 * CPU_GRF registers
*/
#define GRF_CPU_COREPVTPLL_CON0               (CPU_GRF + 0x0010)
#define  CORE_PVTPLL_RING_LENGTH_SEL_SHIFT    3
#define  CORE_PVTPLL_RING_LENGTH_SEL_MASK     (0x1FU << CORE_PVTPLL_RING_LENGTH_SEL_SHIFT)
#define  CORE_PVTPLL_OSC_EN                   BIT1
#define  CORE_PVTPLL_START                    BIT0

/*
 * PMU registers
 */
#define PMU_NOC_AUTO_CON0                     (PMU_BASE + 0x0070)
#define PMU_NOC_AUTO_CON1                     (PMU_BASE + 0x0074)

STATIC CONST GPIO_IOMUX_CONFIG mGmac1IomuxConfig[] = {
  { "gmac1_mdcm0",        3, GPIO_PIN_PC4, 3, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "gmac1_mdiom0",       3, GPIO_PIN_PC5, 3, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "gmac1_txd0m0",       3, GPIO_PIN_PB5, 3, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_2 },
  { "gmac1_txd1m0",       3, GPIO_PIN_PB6, 3, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_2 },
  { "gmac1_txenm0",       3, GPIO_PIN_PB7, 3, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "gmac1_rxd0m0",       3, GPIO_PIN_PB1, 3, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "gmac1_rxd1m0",       3, GPIO_PIN_PB2, 3, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "gmac1_rxdvcrsm0",    3, GPIO_PIN_PB3, 3, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "gmac1_rxclkm0",      3, GPIO_PIN_PA7, 3, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "gmac1_txclkm0",      3, GPIO_PIN_PA6, 3, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_1 },
  { "gmac1_mclkinoutm0",  3, GPIO_PIN_PC0, 3, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "gmac1_rxd2m0",       3, GPIO_PIN_PA4, 3, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "gmac1_rxd3m0",       3, GPIO_PIN_PA5, 3, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "gmac1_txd2m0",       3, GPIO_PIN_PA2, 3, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_2 },
  { "gmac1_txd3m0",       3, GPIO_PIN_PA3, 3, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_2 },
};

STATIC
VOID
BoardInitGmac (
  VOID
  )
{
  UINT8 OtpData[32];
  UINT8 Hash[SHA256_DIGEST_SIZE];
  UINT32 MacLo, MacHi;

  /* Assert reset */
  CruAssertSoftReset (14, 12);

  /* Configure pins */
  GpioSetIomuxConfig (mGmac1IomuxConfig, ARRAY_SIZE (mGmac1IomuxConfig));

  /* Setup clocks */
  MmioWrite32 (CRU_CLKSEL_CON (33), 0x00370004);  // Set rmii1_mode to rgmii mode
                                                  // Set rgmii1_clk_sel to 125M
                                                  // Set rmii1_extclk_sel to mac1 clock from IO

  /* Configure GMAC1 */
  MmioWrite32 (GRF_MAC1_CON0,
               0x7F7F0000U |
               (TX_DELAY << CLK_TX_DL_CFG_SHIFT) |
               (RX_DELAY << CLK_RX_DL_CFG_SHIFT));
  MmioWrite32 (GRF_MAC1_CON1,
               ((PHY_INTF_SEL_MASK | TXCLK_DLY_ENA | RXCLK_DLY_ENA) << 16) |
               PHY_INTF_SEL_RGMII |
               TXCLK_DLY_ENA |
               RXCLK_DLY_ENA);

  /* Reset PHY */
  GpioPinSetDirection (0, GPIO_PIN_PC3, GPIO_PIN_OUTPUT);
  MicroSecondDelay (1000);
  GpioPinWrite (0, GPIO_PIN_PC3, 0);
  MicroSecondDelay (20000);
  GpioPinWrite (0, GPIO_PIN_PC3, 1);
  MicroSecondDelay (100000);

  /* Deassert reset */
  CruDeassertSoftReset (14, 12);

  /* Generate a MAC address from the first 32 bytes in the OTP and write it to GMAC */
  OtpRead (0x00, sizeof (OtpData), OtpData);
  Sha256HashAll (OtpData, sizeof (OtpData), Hash);
  Hash[0] &= 0xFE;
  Hash[0] |= 0x02;
  DEBUG ((DEBUG_INFO, "BOARD: MAC address %02X:%02X:%02X:%02X:%02X:%02X\n",
          Hash[0], Hash[1], Hash[2],
          Hash[3], Hash[4], Hash[5]));
  MacLo = Hash[0] | (Hash[1] << 8) | (Hash[2] << 16) | (Hash[3] << 24);
  MacHi = Hash[4] | (Hash[5] << 8);
  MmioWrite32 (GMAC1_MAC_ADDRESS0_LOW, MacLo);
  MmioWrite32 (GMAC1_MAC_ADDRESS0_HIGH, MacHi);

  /* Initialize Ethernet PHY */
  EthernetPhyInit (GMAC1_BASE);
}

STATIC
EFI_STATUS
PmicRead (
  IN UINT8 Register,
  OUT UINT8 *Value
  )
{
  return I2cRead (I2C0_BASE, PMIC_I2C_ADDR,
                  &Register, sizeof (Register),
                  Value, sizeof (*Value));
}

STATIC
EFI_STATUS
PmicWrite (
  IN UINT8 Register,
  IN UINT8 Value
  )
{
  return I2cWrite (I2C0_BASE, PMIC_I2C_ADDR,
                  &Register, sizeof (Register),
                  &Value, sizeof (Value));
}

STATIC
VOID
BoardInitPmic (
  VOID
  )
{
  EFI_STATUS Status;
  UINT16 ChipName;
  UINT8 ChipVer;
  UINT8 Value;

  DEBUG ((DEBUG_INFO, "BOARD: PMIC init\n"));

  GpioPinSetPull (0, GPIO_PIN_PB1, GPIO_PIN_PULL_NONE);
  GpioPinSetInput (0, GPIO_PIN_PB1, GPIO_PIN_INPUT_SCHMITT);
  GpioPinSetFunction (0, GPIO_PIN_PB1, 1);
  GpioPinSetPull (0, GPIO_PIN_PB2, GPIO_PIN_PULL_NONE);
  GpioPinSetInput (0, GPIO_PIN_PB2, GPIO_PIN_INPUT_SCHMITT);
  GpioPinSetFunction (0, GPIO_PIN_PB2, 1);

  Status = PmicRead (PMIC_CHIP_NAME, &Value);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "Failed to read PMIC chip name! %r\n", Status));
    ASSERT (FALSE);
  }
  ChipName = (UINT16)Value << 4;

  Status = PmicRead (PMIC_CHIP_VER, &Value);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "Failed to read PMIC chip version! %r\n", Status));
    ASSERT (FALSE);
  }
  ChipName |= (Value >> 4) & 0xF;
  ChipVer = Value & 0xF;

  DEBUG ((DEBUG_INFO, "PMIC: Detected RK%03X ver 0x%X\n", ChipName, ChipVer));
  ASSERT (ChipName == 0x817);

  /* Initialize PMIC */
  PmicWrite (PMIC_LDO1_ON_VSEL, 0x30);  /* 1.8V - vcca1v8_pmu */
  PmicWrite (PMIC_LDO2_ON_VSEL, 0x0c);  /* 0.9V - vdda_0v9 */
  PmicWrite (PMIC_LDO3_ON_VSEL, 0x0c);  /* 0.9V - vdd0v9_pmu */
  PmicWrite (PMIC_LDO4_ON_VSEL, 0x6c);  /* 3.3V - vccio_acodec */
  /* Skip LDO5 for now; 1.8V/3.3V - vccio_sd */
  PmicWrite (PMIC_LDO6_ON_VSEL, 0x6c);  /* 3.3V - vcc3v3_pmu */
  PmicWrite (PMIC_LDO7_ON_VSEL, 0x30);  /* 1.8V - vcca_1v8 */
  PmicWrite (PMIC_LDO8_ON_VSEL, 0x30);  /* 1.8V - vcca1v8_dvp */
  PmicWrite (PMIC_LDO9_ON_VSEL, 0x58);  /* 2.8V - vcca2v8_dvp */

  PmicWrite (PMIC_POWER_EN1, 0xff); /* LDO1, LDO2, LDO3, LDO4 */
  PmicWrite (PMIC_POWER_EN2, 0xee); /* LDO6, LDO7, LDO8 */
  PmicWrite (PMIC_POWER_EN3, 0x33); /* LDO9, BOOST */
}

EFI_STATUS
EFIAPI
BoardInitDriverEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  DEBUG ((DEBUG_INFO, "BOARD: BoardInitDriverEntryPoint() called\n"));

  SocSetDomainVoltage (PMUIO2, VCC_3V3);  /* VCC3V3_PMU */
  SocSetDomainVoltage (VCCIO1, VCC_3V3);  /* VCCIO_ACODEC (PCIe) */
  SocSetDomainVoltage (VCCIO2, VCC_1V8);  /* VCCIO_FLASH */
  SocSetDomainVoltage (VCCIO3, VCC_3V3);  /* VCCIO_SD */
  SocSetDomainVoltage (VCCIO4, VCC_1V8);
  SocSetDomainVoltage (VCCIO5, VCC_1V8);
  SocSetDomainVoltage (VCCIO6, VCC_1V8);
  SocSetDomainVoltage (VCCIO7, VCC_3V3);

  BoardInitPmic ();

  /* Set GPIO0 PD3 (WORK_LED) output high to enable LED */
  GpioPinSetDirection (0, GPIO_PIN_PD3, GPIO_PIN_OUTPUT);
  GpioPinWrite (0, GPIO_PIN_PD3, TRUE);

  /* Enable automatic clock gating */
  MmioWrite32 (PMU_NOC_AUTO_CON0, 0xFFFFFFFFU);
  MmioWrite32 (PMU_NOC_AUTO_CON1, 0x000F000FU);

  /* Set core_pvtpll ring length */
  MmioWrite32 (GRF_CPU_COREPVTPLL_CON0,
               ((CORE_PVTPLL_RING_LENGTH_SEL_MASK | CORE_PVTPLL_OSC_EN | CORE_PVTPLL_START) << 16) |
               (5U << CORE_PVTPLL_RING_LENGTH_SEL_SHIFT) | CORE_PVTPLL_OSC_EN | CORE_PVTPLL_START);

  /* Configure MULTI-PHY modes */
  MultiPhySetMode (0, MULTIPHY_MODE_USB3);
  if (PcdGet32 (PcdMultiPhy1Mode) == MULTIPHY_MODE_SEL_USB3) {
    MultiPhySetMode (1, MULTIPHY_MODE_USB3);
  } else {
    ASSERT (PcdGet32 (PcdMultiPhy1Mode) == MULTIPHY_MODE_SEL_SATA);
    MultiPhySetMode (1, MULTIPHY_MODE_SATA);
  }

  /* Set GPIO4 PB5 (USB_HOST_PWREN) output high to power USB ports */
  GpioPinSetDirection (4, GPIO_PIN_PB5, GPIO_PIN_OUTPUT);
  GpioPinWrite (4, GPIO_PIN_PB5, TRUE);

  /* GMAC setup */
  BoardInitGmac ();

  return EFI_SUCCESS;
}
