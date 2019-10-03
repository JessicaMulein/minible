#pragma once
#include <inttypes.h>

typedef struct {} Sercom;
typedef struct {} RTC_MODE2_CLOCK_Type;

#define SERCOM0 (0)
#define SERCOM1 (0)
#define SERCOM2 (0)
#define SERCOM3 (0)
#define SERCOM4 (0)
#define SERCOM5 (0)
#define NVMCTRL_ROW_SIZE (256)

static inline void cpu_irq_enter_critical(void) {}
static inline void cpu_irq_leave_critical(void) {}

static inline uint16_t swap16(uint16_t v) {
	return (v<<8)|(v>>8);
}

extern struct emu_port_t {
	struct {
		struct {
			uint32_t reg;
		} OUTCLR, OUTSET;
	} Group[1];
} *PORT;
