	.set noreorder

#include "pspstub.s"

	// Exact export NIDs from the already-proven GE4 kernel bridge.
	STUB_START "ge4wrap",0x40090000,0x00030005
	STUB_FUNC  0x4069B2E3,ge4ProbeGetEdramHwSize
	STUB_FUNC  0x84F15B3F,ge4ProbeGetModel
	STUB_FUNC  0xD0FE69C4,ge4ProbeSetEdramSize
	STUB_END
