	.set noreorder

#include "pspstub.s"

	// 6.60 sceGe_driver NIDs, byte-identical to the proven GE4 bridge.
	STUB_START "sceGe_driver", 0x40090000, 0x00020005
	STUB_FUNC  0x547EC5F0, sceGeEdramGetHwSize
	STUB_FUNC  0xD8633888, sceGeEdramSetSize
	STUB_END
