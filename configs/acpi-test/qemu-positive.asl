DefinitionBlock ("", "SSDT", 2, "MECOCO", "ACPITEST", 0x00000001)
{
	External (\_SB_, DeviceObj)
	External (\_SB_.PCI0, DeviceObj)

	Scope (\_SB_)
	{
		Name (MVAL, 0x2A)

		Method (MZER, 0, NotSerialized)
		{
			Return (0x42)
		}

		Method (MINT, 2, NotSerialized)
		{
			Return (Add (Arg0, Arg1))
		}

		Method (MBUF, 1, NotSerialized)
		{
			Return (Arg0)
		}

		Method (MPKG, 1, NotSerialized)
		{
			Return (Package (0x03)
			{
				Arg0,
				Buffer (0x02) { 0xA5, 0x5A },
				Package (One) { 0x33 }
			})
		}

		Method (MNST, 1, Serialized)
		{
			Return (MINT (MINT (Arg0, One), One))
		}

		Method (MREF, 1, NotSerialized)
		{
			Return (DerefOf (Arg0))
		}

		Method (M7AR, 7, NotSerialized)
		{
			Return (Arg6)
		}

		Method (MLOP, 1, NotSerialized)
		{
			While (Arg0)
			{
				Noop
			}
			Return (Zero)
		}

		Method (MREC, 1, NotSerialized)
		{
			If (Arg0)
			{
				Return (MREC (Arg0))
			}
			Return (Zero)
		}

		Method (MUNS, 0, NotSerialized)
		{
			Sleep (One)
			Return (Zero)
		}

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
			Name (_DSD, Package ()
			{
				ToUUID ("107ededd-d381-4fd7-8da9-08e9a6c79644"),
				Package ()
				{
					Package (0x02) { "ignored", 0x55 }
				},
				ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
				Package ()
				{
					Package (0x02) { "mcoa-integer", 0x1234 },
					Package (0x02) { "mcoa-string", "ready" },
					Package (0x02) { "mcoa-buffer", Buffer (0x02) { 0xA5, 0x5A } },
					Package (0x02) { "mcoa-array", Package (0x03) { One, "two", \_SB_.PCI0.MI2C } },
					Package (0x02) { "mcoa-reference", \_SB_.PCI0.MI2C },
					Package (0x02) { "clock-frequency", 0x000186A0 }
				}
			})

			Method (_DSM, 4, Serialized)
			{
				If (LEqual (Arg0, ToUUID ("3cdff6f7-4267-4555-ad05-b30a3d8938de")))
				{
					If (LEqual (Arg1, One))
					{
						If (LEqual (Arg2, Zero))
						{
							Return (Buffer (One) { 0x1F })
						}
						If (LEqual (Arg2, One))
						{
							Return (0x20)
						}
						If (LEqual (Arg2, 0x02))
						{
							Return (Buffer (0x02) { 0xA5, 0x5A })
						}
						If (LEqual (Arg2, 0x03))
						{
							Return (Arg3)
						}
						If (LEqual (Arg2, 0x04))
						{
							Return ("bad")
						}
					}
					Return (Buffer (One) { 0x00 })
				}

				If (LEqual (Arg0, ToUUID ("107ededd-d381-4fd7-8da9-08e9a6c79644")))
				{
					If (LEqual (Arg1, One))
					{
						If (LEqual (Arg2, Zero))
						{
							Return (Buffer (One) { 0x03 })
						}
						If (LEqual (Arg2, One))
						{
							Return (0x55)
						}
					}
					Return (Buffer (One) { 0x00 })
				}

				If (LEqual (Arg0, ToUUID ("893f00a6-660c-494e-bcfd-3043f4fb67c0")))
				{
					Return (Zero)
				}
				Return (Buffer (One) { 0x00 })
			}

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

		Device (MDSD)
		{
			Name (_HID, "MCOA0008")
			Name (_STA, 0x0F)
			Method (_DSD, 0, NotSerialized)
			{
				Return (Package ()
				{
					ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
					Package ()
					{
						Package (0x02) { "method-value", 0x77 }
					}
				})
			}
		}

		Device (MDUP)
		{
			Name (_HID, "MCOA0009")
			Name (_STA, 0x0F)
			Name (_DSD, Package ()
			{
				ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
				Package ()
				{
					Package (0x02) { "duplicate", One },
					Package (0x02) { "duplicate", 0x02 }
				}
			})
		}

		Device (MBAD)
		{
			Name (_HID, "MCOA000A")
			Name (_STA, 0x0F)
			Name (_DSD, Package ()
			{
				ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301")
			})
		}

		Device (MDUD)
		{
			Name (_HID, "MCOA000B")
			Name (_STA, 0x0F)
			Name (_DSD, Package ()
			{
				ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
				Package () {},
				ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
				Package () {}
			})
		}

		Device (MUID)
		{
			Name (_HID, "MCOA000C")
			Name (_STA, 0x0F)
			Name (_DSD, Package ()
			{
				Buffer (0x0F) {},
				Package () {}
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
