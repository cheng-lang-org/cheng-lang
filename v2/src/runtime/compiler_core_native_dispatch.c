#include <stdio.h>
#include <string.h>

#include "../../../src/runtime/native/system_helpers.h"
#include "../../bootstrap/cheng_v2c_tooling.h"
#include "compiler_core_native_dispatch.h"

void cheng_v2_native_print_line(const char *text) {
    puts(text);
}

void cheng_v2_native_print_usage(void) {
    cheng_v2_native_print_line("cheng_v2");
    cheng_v2_native_print_line("usage: cheng_v2 <command>");
    cheng_v2_native_print_line("");
    cheng_v2_native_print_line("commands:");
    cheng_v2_native_print_line("  help                         show this message");
    cheng_v2_native_print_line("  status                       print the current tooling-track contract");
    cheng_v2_native_print_line("  <tooling-command>            forward to cheng_tooling_v2");
    cheng_v2_native_print_line("");
    cheng_v2_native_print_line("tooling commands:");
    cheng_v2_native_print_line("  stage-selfhost-host          build stage1 -> stage2 -> stage3 native closure");
    cheng_v2_native_print_line("  tooling-selfhost-host        run the native tooling selfhost orchestration");
    cheng_v2_native_print_line("  tooling-selfhost-check       verify tooling stage0 -> stage1 -> stage2 fixed-point closure");
    cheng_v2_native_print_line("  program-selfhost-check       verify ordinary programs are built and run by a Cheng-produced compiler");
    cheng_v2_native_print_line("  release-compile              emit a deterministic release artifact spec");
    cheng_v2_native_print_line("  lsmr-address                 emit a deterministic LSMR address");
    cheng_v2_native_print_line("  lsmr-route-plan              emit a deterministic canonical LSMR route plan");
    cheng_v2_native_print_line("  outline-parse                emit a compiler_core outline parse");
    cheng_v2_native_print_line("  machine-plan                 emit a deterministic machine plan");
    cheng_v2_native_print_line("  obj-image                    emit a deterministic object image");
    cheng_v2_native_print_line("  obj-file                     emit a deterministic object-file byte layout");
    cheng_v2_native_print_line("  system-link-plan             emit a deterministic native system-link plan");
    cheng_v2_native_print_line("  system-link-exec             materialize object files and invoke the deterministic system linker");
    cheng_v2_native_print_line("  resolve-manifest             resolve a deterministic source manifest");
    cheng_v2_native_print_line("  publish-source-manifest      emit a source manifest artifact");
    cheng_v2_native_print_line("  publish-rule-pack            emit a rule-pack artifact");
    cheng_v2_native_print_line("  publish-compiler-rule-pack   emit a compiler rule-pack artifact");
    cheng_v2_native_print_line("  verify-network-selfhost      verify manifest + topology + rule-pack closure");
}

int cheng_v2_native_print_status(void) {
    cheng_v2_native_print_line("cheng_v2: tooling entry");
    cheng_v2_native_print_line("track=tooling");
    cheng_v2_native_print_line("execution=native_tooling_argv_bridge");
    cheng_v2_native_print_line("tooling_forwarded=1");
    cheng_v2_native_print_line("tooling_entry=tooling/cheng_tooling_v2");
    cheng_v2_native_print_line("parallel=function_task");
    cheng_v2_native_print_line("soa_index_only=1");
    cheng_v2_native_print_line("infra_surface=1");
    return 0;
}

int cheng_v2_native_tooling_argv_entry(int argc, char **argv) {
    const char *cmd;
    if (argc <= 1) {
        cheng_v2_native_print_usage();
        return 0;
    }
    cmd = argv[1];
    if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0 || strcmp(cmd, "help") == 0) {
        cheng_v2_native_print_usage();
        return 0;
    }
    if (strcmp(cmd, "status") == 0) {
        return cheng_v2_native_print_status();
    }
    return cheng_v2c_tooling_handle(argc, argv);
}

int cheng_v2_native_program_argv_entry(int argc, char **argv) {
    return driver_c_compiler_core_program_argv_bridge((int32_t)argc, (const char **)argv);
}
