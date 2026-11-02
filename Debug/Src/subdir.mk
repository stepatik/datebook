################################################################################
# Automatically-generated file.  Do not edit directly; use project properties.
################################################################################


com.palmsource.eclipse.palmos4dev.compiler.m68k_palmos_C_SRCS += \
${addprefix $(ROOT)/Src/, \
DateAlarm.c \
DateDB.c \
DateDay.c \
DateDisplay.c \
DateGlobals.c \
DateMonth.c \
DatePref.c \
DateWeek.c \
Datebook.c \
}

Src/%.o: $(ROOT)/Src/%.c
	@echo
	m68k-palmos-gcc -c -D__PALMOS__=0x03500000 -O2 -g -fexceptions -UNDEBUG -Wall $(subst sdk-,-palmos,$(SDK_VERSION)) -o $@  $<
	echo -n $(@:%.o=%.d) 'Src/' > $(@:%.o=%.d) && \
	m68k-palmos-gcc -c -MM -MG -P -w -D__PALMOS__=0x03500000 -O2 -g -fexceptions -UNDEBUG -Wall $(subst sdk-,-palmos,$(SDK_VERSION))  $< >> $(@:%.o=%.d)


