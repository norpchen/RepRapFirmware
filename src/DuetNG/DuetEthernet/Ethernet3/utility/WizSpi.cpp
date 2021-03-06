/*
 * WizSpi.cpp
 *
 *  Created on: 16 Dec 2016
 *      Author: David
 */

#include "WizSpi.h"
#include "variant.h"
#include "Pins.h"

// Define exactly one of the following as 1, the other as zero
// The PDC seems to be too slow to work reliably without getting transmit underruns, so we use the DMAC now.
#define USE_PDC		0		// use peripheral DMA controller
#define USE_DMAC	0		// use general DMA controller

#if USE_PDC
#include "pdc.h"
#endif

#if USE_DMAC
#include "dmac.h"
#endif

#include "matrix.h"

// Functions called by the W5500 module to transfer data to/from the W5500 via SPI
const uint32_t SpiClockFrequency = 10000000;			// use 10MHz for now, increase it later
const unsigned int SpiPeripheralChannelId = 0;			// we use NPCS0 as the slave select signal

#if USE_PDC
static Pdc *spi_pdc;
static inline void spi_rx_dma_enable()
{
	pdc_enable_transfer(spi_pdc, PERIPH_PTCR_RXTEN);
}

static inline void spi_tx_dma_enable()
{
	pdc_enable_transfer(spi_pdc, PERIPH_PTCR_TXTEN);
}

static inline void spi_rx_dma_disable()
{
	pdc_disable_transfer(spi_pdc, PERIPH_PTCR_RXTDIS);
}

static inline void spi_tx_dma_disable()
{
	pdc_disable_transfer(spi_pdc, PERIPH_PTCR_TXTDIS);
}

static bool spi_dma_check_rx_complete()
{
	return true;
}

static void spi_tx_dma_setup(const TransactionBuffer *buf, uint32_t maxTransmitLength)
{
	pdc_packet_t pdc_spi_packet;
	pdc_spi_packet.ul_addr = reinterpret_cast<uint32_t>(buf);
	pdc_spi_packet.ul_size = buf->PacketLength() * 4;			// need length in bytes
	pdc_tx_init(spi_pdc, &pdc_spi_packet, NULL);
}

static void spi_rx_dma_setup(const TransactionBuffer *buf)
{
	pdc_packet_t pdc_spi_packet;
	pdc_spi_packet.ul_addr = reinterpret_cast<uint32_t>(buf);
	pdc_spi_packet.ul_size = TransactionBuffer::MaxReceiveBytes;
	pdc_rx_init(spi_pdc, &pdc_spi_packet, NULL);
}

#endif

#if USE_DMAC

// Our choice of DMA channels to use
const uint32_t CONF_SPI_DMAC_TX_CH = 1;
const uint32_t CONF_SPI_DMAC_RX_CH = 2;

// Hardware IDs of the SPI transmit and receive DMA interfaces. See atsam datasheet.
const uint32_t DMA_HW_ID_SPI_TX = 1;
const uint32_t DMA_HW_ID_SPI_RX = 2;

static inline void spi_rx_dma_enable()
{
	dmac_channel_enable(DMAC, CONF_SPI_DMAC_RX_CH);
}

static inline void spi_tx_dma_enable()
{
	dmac_channel_enable(DMAC, CONF_SPI_DMAC_TX_CH);
}

static inline void spi_rx_dma_disable()
{
	dmac_channel_disable(DMAC, CONF_SPI_DMAC_RX_CH);
}

static inline void spi_tx_dma_disable()
{
	dmac_channel_disable(DMAC, CONF_SPI_DMAC_TX_CH);
}

static bool spi_dma_check_rx_complete()
{
	uint32_t status = DMAC->DMAC_CHSR;
	if (   ((status & (DMAC_CHSR_ENA0 << CONF_SPI_DMAC_RX_CH)) == 0)	// controller is not enabled, perhaps because it finished a full buffer transfer
		|| ((status & (DMAC_CHSR_EMPT0 << CONF_SPI_DMAC_RX_CH)) != 0)	// controller is enabled, probably suspended, and the FIFO is empty
	   )
	{
		// Disable the channel.
		// We also need to set the resume bit, otherwise it remains suspended when we re-enable it.
		DMAC->DMAC_CHDR = (DMAC_CHDR_DIS0 << CONF_SPI_DMAC_RX_CH) | (DMAC_CHDR_RES0 << CONF_SPI_DMAC_RX_CH);
		return true;
	}
	return false;
}

static void spi_tx_dma_setup(const TransactionBuffer *buf, uint32_t maxTransmitLength)
{
	DMAC->DMAC_EBCISR;		// clear any pending interrupts

	dmac_channel_set_source_addr(DMAC, CONF_SPI_DMAC_TX_CH, reinterpret_cast<uint32_t>(buf));
	dmac_channel_set_destination_addr(DMAC, CONF_SPI_DMAC_TX_CH, reinterpret_cast<uint32_t>(& SPI->SPI_TDR));
	dmac_channel_set_descriptor_addr(DMAC, CONF_SPI_DMAC_TX_CH, 0);
	dmac_channel_set_ctrlA(DMAC, CONF_SPI_DMAC_TX_CH, maxTransmitLength | DMAC_CTRLA_SRC_WIDTH_WORD | DMAC_CTRLA_DST_WIDTH_BYTE);
	dmac_channel_set_ctrlB(DMAC, CONF_SPI_DMAC_TX_CH,
		DMAC_CTRLB_SRC_DSCR | DMAC_CTRLB_DST_DSCR | DMAC_CTRLB_FC_MEM2PER_DMA_FC | DMAC_CTRLB_SRC_INCR_INCREMENTING | DMAC_CTRLB_DST_INCR_FIXED);
}

static void spi_rx_dma_setup(const TransactionBuffer *buf)
{
	DMAC->DMAC_EBCISR;		// clear any pending interrupts

	dmac_channel_set_source_addr(DMAC, CONF_SPI_DMAC_RX_CH, reinterpret_cast<uint32_t>(& SPI->SPI_RDR));
	dmac_channel_set_destination_addr(DMAC, CONF_SPI_DMAC_RX_CH, reinterpret_cast<uint32_t>(buf));
	dmac_channel_set_descriptor_addr(DMAC, CONF_SPI_DMAC_RX_CH, 0);
//	dmac_channel_set_ctrlA(DMAC, CONF_SPI_DMAC_RX_CH, TransactionBuffer::MaxTransferBytes | DMAC_CTRLA_SRC_WIDTH_BYTE | DMAC_CTRLA_DST_WIDTH_WORD);
	dmac_channel_set_ctrlB(DMAC, CONF_SPI_DMAC_RX_CH,
		DMAC_CTRLB_SRC_DSCR | DMAC_CTRLB_DST_DSCR | DMAC_CTRLB_FC_PER2MEM_DMA_FC | DMAC_CTRLB_SRC_INCR_FIXED | DMAC_CTRLB_DST_INCR_INCREMENTING);
}

#endif

#if USE_PDC || USE_DMAC

static void spi_dma_disable()
{
	spi_tx_dma_disable();
	spi_rx_dma_disable();
}

#if 0
/**
 * \brief Set SPI slave transfer.
 */
static void spi_slave_dma_setup(bool dataToSend, bool allowReceive)
{
#if USE_PDC
	pdc_disable_transfer(spi_pdc, PERIPH_PTCR_TXTDIS | PERIPH_PTCR_RXTDIS);

	TransactionBuffer *outBufPointer = (dataToSend) ? &outBuffer : reinterpret_cast<TransactionBuffer*>(&dummyOutBuffer);
	spi_tx_dma_setup(outBufPointer);
	if (allowReceive)
	{
		outBufPointer->SetDataTaken();
		spi_rx_dma_setup(&inBuffer);
		pdc_enable_transfer(spi_pdc, PERIPH_PTCR_TXTEN | PERIPH_PTCR_RXTEN);
	}
	else
	{
		outBufPointer->ClearDataTaken();
		pdc_enable_transfer(spi_pdc, PERIPH_PTCR_TXTEN);
	}
#endif

#if USE_DMAC
	spi_dma_disable();

	TransactionBuffer *outBufPointer = (dataToSend) ? &outBuffer : reinterpret_cast<TransactionBuffer*>(&dummyOutBuffer);
	if (allowReceive)
	{
		spi_rx_dma_setup(&inBuffer);
		spi_rx_dma_enable();
//		outBufPointer->SetDataTaken();
	}
	else
	{
//		outBufPointer->ClearDataTaken();
	}

//	spi_tx_dma_setup(outBufPointer, (dataToSend) ? TransactionBuffer::MaxTransferBytes : 4 * TransactionBuffer::headerDwords);
	spi_tx_dma_enable();
#endif
}
#endif

#endif

namespace WizSpi
{
	// Initialise the SPI interface
	void Init()
	{
	#if USE_PDC
		spi_pdc = spi_get_pdc_base(SPI);
		// The PDCs are masters 2 and 3 and the SRAM is slave 0. Give the PDCs the highest priority.
		matrix_set_master_burst_type(0, MATRIX_ULBT_8_BEAT_BURST);
		matrix_set_slave_default_master_type(0, MATRIX_DEFMSTR_LAST_DEFAULT_MASTER);
		matrix_set_slave_priority(0, (3 << MATRIX_PRAS0_M2PR_Pos) | (3 << MATRIX_PRAS0_M3PR_Pos));
		matrix_set_slave_slot_cycle(0, 8);
	#endif

	#if USE_DMAC
		pmc_enable_periph_clk(ID_DMAC);
		dmac_init(DMAC);
		dmac_set_priority_mode(DMAC, DMAC_PRIORITY_ROUND_ROBIN);
		dmac_enable(DMAC);
		// The DMAC is master 4 and the SRAM is slave 0. Give the DMAC the highest priority.
		matrix_set_slave_default_master_type(0, MATRIX_DEFMSTR_LAST_DEFAULT_MASTER);
		matrix_set_slave_priority(0, (3 << MATRIX_PRAS0_M4PR_Pos));
		// Set the slave slot cycle limit.
		// If we leave it at the default value of 511 clock cycles, we get transmit underruns due to the HSMCI using the bus for too long.
		// A value of 8 seems to work. I haven't tried other values yet.
		matrix_set_slave_slot_cycle(0, 8);
	#endif

		// Set up the SPI pins
		ConfigurePin(g_APinDescription[APIN_SPI_SCK]);
		ConfigurePin(g_APinDescription[APIN_SPI_MOSI]);
		ConfigurePin(g_APinDescription[APIN_SPI_MISO]);
#if 1
		pinMode(APIN_SPI_SS0, OUTPUT_HIGH);					// use manual SS control for now
#else
		ConfigurePin(g_APinDescription[APIN_SPI_SS0]);
#endif

		pmc_enable_periph_clk(ID_SPI);

#if USE_PDC || USE_DMAC
		spi_dma_disable();
#endif

		spi_reset(SPI);										// this clears the transmit and receive registers and puts the SPI into slave mode
		SPI->SPI_MR = SPI_MR_MSTR							// master mode
						| SPI_MR_MODFDIS					// disable fault detection
						| SPI_MR_PCS(SpiPeripheralChannelId); // fixed peripheral select

		// Set SPI mode, clock frequency, CS active after transfer, delay between transfers
		const uint16_t baud_div = (uint16_t)spi_calc_baudrate_div(SpiClockFrequency, SystemCoreClock);
		const uint32_t csr = SPI_CSR_SCBR(baud_div)			// Baud rate
						| SPI_CSR_BITS_8_BIT				// Transfer bit width
						| SPI_CSR_DLYBCT(0)      			// Transfer delay
						| SPI_CSR_CSAAT						// Keep CS low after transfer in case we are slow in writing the next byte
						| SPI_CSR_NCPHA;					// Data is captured on the leading edge of the clock (SPI mode 0)
		SPI->SPI_CSR[SpiPeripheralChannelId] = csr;
		spi_enable(SPI);

#if USE_DMAC
		// Configure DMA RX channel
		dmac_channel_set_configuration(DMAC, CONF_SPI_DMAC_RX_CH,
				DMAC_CFG_SRC_PER(DMA_HW_ID_SPI_RX) | DMAC_CFG_SRC_H2SEL | DMAC_CFG_SOD | DMAC_CFG_FIFOCFG_ASAP_CFG);

		// Configure DMA TX channel
		dmac_channel_set_configuration(DMAC, CONF_SPI_DMAC_TX_CH,
				DMAC_CFG_DST_PER(DMA_HW_ID_SPI_TX) | DMAC_CFG_DST_H2SEL | DMAC_CFG_SOD | DMAC_CFG_FIFOCFG_ASAP_CFG);
#endif
	}

	void Stop()
	{
		NVIC_DisableIRQ(SPI_IRQn);
		spi_disable(SPI);
#if USE_PDC || USE_DMA
		spi_dma_check_rx_complete();
		spi_dma_disable();
#endif
	}

	// Wait for transmitter ready returning true if timed out
	static inline bool waitForTxReady()
	{
		uint32_t timeout = SPI_TIMEOUT;
		while (!spi_is_tx_ready(SPI))
		{
			if (--timeout == 0)
			{
				return true;
			}
		}
		return false;
	}

	// Wait for transmitter empty returning true if timed out
	static inline bool waitForTxEmpty()
	{
		uint32_t timeout = SPI_TIMEOUT;
		while (!spi_is_tx_empty(SPI))
		{
			if (!timeout--)
			{
				return true;
			}
		}
		return false;
	}

	// Wait for receive data available returning true if timed out
	static inline bool waitForRxReady()
	{
		uint32_t timeout = SPI_TIMEOUT;
		while (!spi_is_rx_ready(SPI))
		{
			if (--timeout == 0)
			{
				return true;
			}
		}
		return false;
	}

	// Set the SS pin low to address the W5500
	void AssertSS()
	{
		spi_set_peripheral_chip_select_value(SPI, spi_get_pcs(SpiPeripheralChannelId));
		digitalWrite(SamCsPin, LOW);
	}

	// Set the SS pin high again
	void ReleaseSS()
	{
		waitForTxEmpty();
		digitalWrite(SamCsPin, HIGH);
	}

	//TODO return type, error returns
	uint8_t ExchangeByte(uint8_t b, bool isLast)
	{
		if (waitForTxReady())
		{
			return SPI_ERROR_TIMEOUT;
		}

		// Write to transmit register
		uint32_t dOut = b;
		if (isLast)
		{
			dOut |= SPI_TDR_LASTXFER;
		}
		SPI->SPI_TDR = dOut;

		// Wait for receive register
		if (waitForRxReady())
		{
			return SPI_ERROR_TIMEOUT;
		}

		// Get data from receive register
		return (uint8_t)SPI->SPI_RDR;
	}

	spi_status_t ReadBurst(uint8_t* rx_data, size_t len)
	{
		for (size_t i = 0; i < len; ++i)
		{
			uint32_t dOut = 0x000000FF;
			if (waitForTxReady())
			{
				return SPI_ERROR_TIMEOUT;
			}

			// Write to transmit register
			if (i + 1 == len)
			{
				dOut |= SPI_TDR_LASTXFER;
			}
			SPI->SPI_TDR = dOut;

			// Wait for receive register
			if (waitForRxReady())
			{
				return SPI_ERROR_TIMEOUT;
			}

			// Get data from receive register
			*rx_data++ = (uint8_t)SPI->SPI_RDR;
		}

		return SPI_OK;
	}

	spi_status_t SendBurst(const uint8_t* tx_data, size_t len)
	{
		for (uint32_t i = 0; i < len; ++i)
		{
			uint32_t dOut = (uint32_t)*tx_data++;
			if (waitForTxReady())
			{
				return SPI_ERROR_TIMEOUT;
			}

			// Write to transmit register
			if (i + 1 == len)
			{
				dOut |= SPI_TDR_LASTXFER;
			}
			SPI->SPI_TDR = dOut;
			// Wait for receive register
			if (waitForRxReady())
			{
				return SPI_ERROR_TIMEOUT;
			}

			// Get data from receive register
			(void)SPI->SPI_RDR;
		}

		return SPI_OK;
	}

}	// end namespace

// End
