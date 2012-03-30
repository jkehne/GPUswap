uint32_t nvc0_pdaemon_pointers[] = {
	0x00000024,
	0x0000005b,
	0x00000089,
	0x000000b4,
	0x000000e5,
	0x00000121,
	0x0000014c,
	0x00000161,
	0x000001c0,
};

uint32_t nvc0_pdaemon_code[] = {
/* 0x0000: init */
	0xf41032f4,
	0x07f01132,
	0xf114bdff,
	0xf1800013,
	0xf1080027,
	0xfe560037,
	0x30800034,
	0x04339000,
	0x01fc0ef5,
/* 0x0024: done */
	0x90003080,
	0x33800433,
	0x04339000,
	0xdead07f1,
	0xdead03f1,
	0x308000f9,
/* 0x003f: mmsync */
	0xf002f800,
/* 0x0042: mmloop_ */
	0x90f90097,
	0xf7f190fc,
	0xf3f0eb00,
	0x00ffcf01,
	0xc7019990,
	0x1bf44cff,
/* 0x005b: mmwrs */
	0xf900f8ec,
	0x3f21f4a0,
	0xe800f7f1,
	0xd001f3f0,
	0xf0b700fa,
	0xfbd00100,
	0x00f0b700,
	0xf2b7f102,
	0x01b3f000,
	0xcf00fbd0,
	0x21f400fb,
	0xf8a0fc3f,
/* 0x0089: mmwr */
	0xf4a0f900,
	0xf7f13f21,
	0xf3f0e800,
	0x00fad001,
	0x0100f0b7,
	0xb700fbd0,
	0xf10200f0,
	0xf000f2b7,
	0xfbd001b3,
	0x00fbcf00,
	0x00f8a0fc,
/* 0x00b4: mmrd */
	0x21f4a0f9,
	0x00f7f13f,
	0x01f3f0e8,
	0xb700fad0,
	0xf10300f0,
	0xf000f1b7,
	0xfbd001b3,
	0x00fbcf00,
	0xf13f21f4,
	0xf0e900f7,
	0xa0fc01f3,
	0xf800facf,
/* 0x00e5: wait_mask_ext */
	0x02a6b900,
	0xb902b7b9,
	0xd5b902c8,
	0x0097f102,
	0x0094cf0b,
/* 0x00f8: repeat_ext */
	0xf4026ab9,
	0xa7ffb421,
	0x11a7f1c4,
	0x06c8b811,
	0xf1170bf4,
	0xcf0b0097,
	0xa4bc009a,
	0x06a5b8a2,
	0xf1e01ef4,
/* 0x011f: success_ext */
	0xf80999a7,
/* 0x0121: wait_mask_iord */
	0x0097f100,
	0x009ecf0b,
/* 0x0128: repeat_iord */
	0xff00afcf,
	0xfcb8f4fb,
	0x150bf406,
	0xbc009fcf,
	0xfdb8f2fe,
	0xeb1ef406,
	0x4999a7f1,
/* 0x0146: success_iord */
	0xa7f100f8,
	0x00f84111,
/* 0x014c: sleep */
	0x0b00b7f1,
	0xbb00bfcf,
/* 0x0156: sleeploop */
	0xbacf00fa,
	0xc2afbc00,
	0xf8fa1ef4,
/* 0x0161: enter_lock */
	0x20a7f100,
	0xb421f416,
	0xf102a4b9,
	0xfff55d97,
	0xa7f1b4a9,
	0x21f41620,
	0x20a7f15b,
	0xb421f416,
	0xb900aaf0,
	0xa7f102ab,
	0x21f41620,
	0xf0a7f15b,
	0xb421f426,
	0xf002abb9,
	0xa7f100ba,
	0x21f426f0,
	0x00a7f15b,
	0x01a3f0f8,
	0xd004b7f0,
	0xabcf00ab,
	0x00aaa200,
/* 0x01b5: enterloop */
	0x00abcf07,
	0xf404b4f0,
	0x00f8fa0b,
/* 0x01c0: leave_lock */
	0xf900a7f1,
	0xf001a3f0,
	0xabd004b7,
	0x00abcf00,
	0x0800aaa2,
/* 0x01d4: leaveloop */
	0xf000abcf,
	0x1bf404b4,
	0xf0a7f1fa,
	0xb421f426,
	0xf002bab9,
	0xa7f100b9,
	0x21f426f0,
	0x20a7f15b,
	0xb421f416,
	0xb900a9f0,
	0xa7f102ab,
	0x21f41620,
	0x20a7f15b,
	0xb421f416,
	0x0aa2c7f1,
	0xf1b5acff,
	0xf41620a7,
	0x00f85b21,
/* 0x021c: main */
	0x98002998,
	0x2a98012f,
	0x032b9802,
	0x98042c98,
	0x91ff052d,
	0x2029bc04,
	0x80003280,
	0x3a80013f,
	0x0c30b602,
	0x01b8f5f9,
	0xd71bf406,
	0xb6003a80,
	0x0ef40430,
	0x000000ce,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
};
