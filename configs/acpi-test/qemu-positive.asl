DefinitionBlock ("", "SSDT", 2, "MECOCO", "ACPITEST", 0x00000001)
{
	External (\_SB_, DeviceObj)
	External (\_SB_.PCI0, DeviceObj)

	Scope (\_SB_)
	{
		Device (MGPO)
		{
			Name (_HID, "MCOA0005")
			Name (_STA, 0x0F)
		}
	}

	Scope (\_SB_.PCI0)
	{
		Device (MI2C)
		{
			Name (_HID, "MCOA0001")
			Name (_ADR, Zero)
			OperationRegion (PCFG, PCI_Config, Zero, 0x04)
			Field (PCFG, DWordAcc, NoLock, Preserve)
			{
				PCID, 32
			}

			Method (_STA, 0, Serialized)
			{
				Store (PCID, Local0)
				Return (0x0F)
			}

		}

		Device (MSPI)
		{
			Name (_HID, "MCOA0003")
			OperationRegion (SMEM, SystemMemory, 0x00000400, 0x01)
			Field (SMEM, ByteAcc, NoLock, Preserve)
			{
				MDBY, 8
			}

			Method (_STA, 0, NotSerialized)
			{
				Store (MDBY, Local0)
				Return (0x0F)
			}

			Device (MSPD)
			{
				Name (_HID, "MCOA0006")
				Name (_STA, 0x0F)
				Name (_CRS, ResourceTemplate ()
				{
					SpiSerialBusV2 (0x0001, PolarityLow, FourWireMode, 8,
						ControllerInitiated, 10000000, ClockPolarityLow,
						ClockPhaseFirst, "\\_SB.PCI0.MSPI", 0x00,
						ResourceConsumer, , Exclusive,)
				})
			}
		}

		Device (MUAR)
		{
			Name (_HID, "MCOA0004")
			Name (_STA, 0x0F)

			Device (MURT)
			{
				Name (_HID, "MCOA0007")
				Name (_STA, 0x0F)

				Method (_CRS, 0, NotSerialized)
				{
					Name (RBUF, ResourceTemplate ()
					{
						UartSerialBusV2 (115200, DataBitsEight, StopBitsOne,
							0xC0, LittleEndian, ParityTypeNone, FlowControlHardware,
							0x0040, 0x0040, "\\_SB.PCI0.MUAR", 0x00,
							ResourceConsumer, , Exclusive,)
					})
					Return (RBUF)
				}
			}
		}
	}

	Scope (\_SB_.PCI0.MI2C)
	{
		Device (MTPD)
		{
			Name (_HID, "ALPS0001")
			Name (_CID, "PNP0C50")
			Name (_UID, One)
			Name (SBFB, ResourceTemplate ()
			{
				I2cSerialBusV2 (0x002C, ControllerInitiated, 400000,
					AddressingMode7Bit, "\\_SB.PCI0.MI2C", 0x00,
					ResourceConsumer, , Exclusive,)
			})
			Name (SBFG, ResourceTemplate ()
			{
				GpioInt (Level, ActiveLow, ExclusiveAndWake, PullDefault, 0x0000,
					"\\_SB.MGPO", 0x00, ResourceConsumer, ,)
				{
					0x0014
				}
			})

			Method (_STA, 0, NotSerialized)
			{
				Return (0x0F)
			}

			Method (_CRS, 0, NotSerialized)
			{
				Return (ConcatenateResTemplate (SBFB, SBFG))
			}
		}

		Device (MOFF)
		{
			Name (_HID, "MCOA00FF")
			Name (_STA, Zero)
			Name (_CRS, ResourceTemplate ()
			{
				I2cSerialBusV2 (0x003E, ControllerInitiated, 100000,
					AddressingMode7Bit, "\\_SB.PCI0.MI2C", 0x00,
					ResourceConsumer, , Exclusive,)
			})
		}
	}

	If (One)
	{
		Scope (\_SB_.PCI0.MI2C)
		{
			Name (_DEP, Package (One)
			{
				\_SB_.MGPO
			})
		}
	}
}
