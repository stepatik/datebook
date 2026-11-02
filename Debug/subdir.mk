################################################################################
# Automatically-generated file.  Do not edit directly; use project properties.
################################################################################


$(MULTISECTION_OBJS): $(ROOT)/DateBook.def 
	@echo
	m68k-palmos-multigen -b multiple_code_sections  $(ROOT)/DateBook.def 

multiple_code_sections.s: $(ROOT)/DateBook.def 
	@echo
	m68k-palmos-multigen -b multiple_code_sections  $(ROOT)/DateBook.def 

