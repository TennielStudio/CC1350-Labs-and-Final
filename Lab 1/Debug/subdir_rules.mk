################################################################################
# Automatically-generated file. Do not edit!
################################################################################

SHELL = cmd.exe

# Each subdirectory must supply rules for building sources it contributes
%.obj: ../%.c $(GEN_OPTS) | $(GEN_FILES)
	@echo 'Building file: "$<"'
	@echo 'Invoking: ARM Compiler'
	"C:/ti/ccs910/ccs/tools/compiler/ti-cgt-arm_19.6.0.STS/bin/armcl" -mv7M3 --code_state=16 --float_support=vfplib -me --include_path="C:/NewCC/hello_CC1350_LAUNCHXL_tirtos_ccs" --include_path="C:/ti/simplelink_cc13x0_sdk_3_20_00_23/source/ti/posix/ccs" --include_path="C:/ti/ccs910/ccs/tools/compiler/ti-cgt-arm_19.6.0.STS/include" --define=DeviceFamily_CC13X0 -g --diag_warning=225 --diag_warning=255 --diag_wrap=off --display_error_number --gen_func_subsections=on --preproc_with_compile --preproc_dependency="$(basename $(<F)).d_raw" $(GEN_OPTS__FLAG) "$<"
	@echo 'Finished building: "$<"'
	@echo ' '

build-1397698895:
	@$(MAKE) --no-print-directory -Onone -f subdir_rules.mk build-1397698895-inproc

build-1397698895-inproc: ../hello.cfg
	@echo 'Building file: "$<"'
	@echo 'Invoking: XDCtools'
	"C:/ti/xdctools_3_51_03_28_core/xs" --xdcpath="C:/ti/simplelink_cc13x0_sdk_3_20_00_23/source;C:/ti/simplelink_cc13x0_sdk_3_20_00_23/kernel/tirtos/packages;" xdc.tools.configuro -o configPkg -t ti.targets.arm.elf.M3 -p ti.platforms.simplelink:CC1350F128 -r release -c "C:/ti/ccs910/ccs/tools/compiler/ti-cgt-arm_19.6.0.STS" --compileOptions "-mv7M3 --code_state=16 --float_support=vfplib -me --include_path=\"C:/NewCC/hello_CC1350_LAUNCHXL_tirtos_ccs\" --include_path=\"C:/ti/simplelink_cc13x0_sdk_3_20_00_23/source/ti/posix/ccs\" --include_path=\"C:/ti/ccs910/ccs/tools/compiler/ti-cgt-arm_19.6.0.STS/include\" --define=DeviceFamily_CC13X0 -g --diag_warning=225 --diag_warning=255 --diag_wrap=off --display_error_number --gen_func_subsections=on  " "$<"
	@echo 'Finished building: "$<"'
	@echo ' '

configPkg/linker.cmd: build-1397698895 ../hello.cfg
configPkg/compiler.opt: build-1397698895
configPkg/: build-1397698895


