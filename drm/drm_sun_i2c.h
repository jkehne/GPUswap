/* BEGIN CSTYLED */
#ifndef __DRM_I2C_H__
#define __DRM_I2C_H__

#include <sys/ksynch.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/cmn_err.h>
#include "drm.h"

#define I2C_SMBUS_BLOCK_MAX	32	/* As specified in SMBus standard */

#define I2C_FUNC_I2C			0x00000001
#define I2C_FUNC_10BIT_ADDR		0x00000002
#define I2C_FUNC_PROTOCOL_MANGLING	0x00000004 /* I2C_M_NOSTART etc. */
#define I2C_FUNC_SMBUS_PEC		0x00000008
#define I2C_FUNC_SMBUS_BLOCK_PROC_CALL	0x00008000 /* SMBus 2.0 */
#define I2C_FUNC_SMBUS_QUICK		0x00010000
#define I2C_FUNC_SMBUS_READ_BYTE	0x00020000
#define I2C_FUNC_SMBUS_WRITE_BYTE	0x00040000
#define I2C_FUNC_SMBUS_READ_BYTE_DATA	0x00080000
#define I2C_FUNC_SMBUS_WRITE_BYTE_DATA	0x00100000
#define I2C_FUNC_SMBUS_READ_WORD_DATA	0x00200000
#define I2C_FUNC_SMBUS_WRITE_WORD_DATA	0x00400000
#define I2C_FUNC_SMBUS_PROC_CALL	0x00800000
#define I2C_FUNC_SMBUS_READ_BLOCK_DATA	0x01000000
#define I2C_FUNC_SMBUS_WRITE_BLOCK_DATA 0x02000000
#define I2C_FUNC_SMBUS_READ_I2C_BLOCK	0x04000000 /* I2C-like block xfer  */
#define I2C_FUNC_SMBUS_WRITE_I2C_BLOCK	0x08000000 /* w/ 1-byte reg. addr. */
#define I2C_FUNC_SMBUS_BYTE		(I2C_FUNC_SMBUS_READ_BYTE | \
					 I2C_FUNC_SMBUS_WRITE_BYTE)
#define I2C_FUNC_SMBUS_BYTE_DATA	(I2C_FUNC_SMBUS_READ_BYTE_DATA | \
					 I2C_FUNC_SMBUS_WRITE_BYTE_DATA)
#define I2C_FUNC_SMBUS_WORD_DATA	(I2C_FUNC_SMBUS_READ_WORD_DATA | \
					 I2C_FUNC_SMBUS_WRITE_WORD_DATA)
#define I2C_FUNC_SMBUS_BLOCK_DATA	(I2C_FUNC_SMBUS_READ_BLOCK_DATA | \
					 I2C_FUNC_SMBUS_WRITE_BLOCK_DATA)
#define I2C_FUNC_SMBUS_I2C_BLOCK	(I2C_FUNC_SMBUS_READ_I2C_BLOCK | \
					 I2C_FUNC_SMBUS_WRITE_I2C_BLOCK)

#define I2C_FUNC_SMBUS_EMUL		(I2C_FUNC_SMBUS_QUICK | \
					 I2C_FUNC_SMBUS_BYTE | \
					 I2C_FUNC_SMBUS_BYTE_DATA | \
					 I2C_FUNC_SMBUS_WORD_DATA | \
					 I2C_FUNC_SMBUS_PROC_CALL | \
					 I2C_FUNC_SMBUS_WRITE_BLOCK_DATA | \
					 I2C_FUNC_SMBUS_I2C_BLOCK | \
					 I2C_FUNC_SMBUS_PEC)

#define I2C_SMBUS_QUICK		    0
#define I2C_SMBUS_BYTE		    1
#define I2C_SMBUS_BYTE_DATA	    2
#define I2C_SMBUS_WORD_DATA	    3
#define I2C_SMBUS_PROC_CALL	    4
#define I2C_SMBUS_BLOCK_DATA	    5
#define I2C_SMBUS_I2C_BLOCK_BROKEN  6
#define I2C_SMBUS_BLOCK_PROC_CALL   7		/* SMBus 2.0 */
#define I2C_SMBUS_I2C_BLOCK_DATA    8


struct i2c_adapter;
struct i2c_msg;
struct i2c_algorithm {
	int (*master_xfer)(struct i2c_adapter *adap, struct i2c_msg *msgs, int num);
//	int (*smbus_xfer) (struct i2c_adapter *adap, u16 addr,
//			   unsigned short flags, char read_write,
//			   u8 command, int size, union i2c_smbus_data *data);

	/* To determine what the adapter supports */
	u32 (*functionality) (struct i2c_adapter *);
};
struct i2c_adapter {
	//struct module *owner;
	unsigned int id;
	unsigned int class;		  /* classes to allow probing for */
	struct i2c_algorithm *algo;
	/* data fields that are valid for all devices	*/
	__u8 level; 			/* nesting level for lockdep */
	kmutex_t bus_lock;

	clock_t timeout;			/* in jiffies */
	int retries;
//	struct device dev;		/* the adapter device */

	int nr;
	char name[48];
//	struct completion dev_released;

	void *data;		/* private data for lowlevel routines */
	void (*setsda) (void *data, int state);
	void (*setscl) (void *data, int state);
	int  (*getsda) (void *data);
	int  (*getscl) (void *data);

	void *algo_data;       
	/* local settings */
	clock_t udelay;		/* half clock cycle time in us,
				   minimum 2 us for fast-mode I2C,
				   minimum 5 us for standard-mode I2C and SMBus,
				   maximum 50 us for SMBus */
//	int timeout;		/* in jiffies */
	
};

#define I2C_M_TEN		0x0010	/* this is a ten bit chip address */
#define I2C_M_RD		0x0001	/* read data, from slave to master */
#define I2C_M_NOSTART		0x4000	/* if I2C_FUNC_PROTOCOL_MANGLING */

struct i2c_msg {
	__u16 addr;	/* slave address			*/
	__u16 flags;
	__u16 len;	/* msg length				*/
	__u8 *buf;	/* pointer to msg data			*/
};

extern int i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num);
extern int i2c_bit_add_bus(struct i2c_adapter *adap);

#endif
