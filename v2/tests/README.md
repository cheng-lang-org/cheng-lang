# v2 Tests

The new compiler tree keeps its own tests under `v2/tests/`.

Initial focus:

- contract probes
- pipeline surface probes
- deterministic merge probes
- ABI bridge surface probes
- string runtime surface and algorithm probes
- string lowering and low_uir string op probes
- string end-to-end pipeline and SoA layout probes
- Unimaker surface and pipeline probes for actor/state/hashref/capability/refinement/evolve
- topology and network-distribution surface/pipeline probes
- formal LSMR address/route-plan probes and checked-in command artifacts
- compiler-core outline, pipeline, machine, obj-image, system-link-plan, system-link-exec surface, and release-artifact probes for tooling/runtime source modules
- runtime-provider-object probes for formal provider object materialization
- compiler-core native provider source-of-truth probes through `src/runtime/compiler_core_native_provider.c`
- runtime-provider identity probes for `source_path / source_kind / compile_mode / execution_model / trace_symbol / provider_symbol_count`
- `full-selfhost` regression now also pins `stage{1,2,3}_external_cc_provider_count` and
  `compiler_core_provider_source_kind / compiler_core_provider_compile_mode /
   compiler_core_dispatch_provider_removed`
- `full-selfhost` is expected to fail until those fields prove that the
  `compiler_core` external-C provider has been removed
- dev-entry probes for `cheng_v2 -> cheng_tooling_v2` command forwarding
- source-manifest and release-artifact compile-key probes
- machine-pipeline, object-image, and object-file surface probes
- bootstrap compiler smoke through `v2/bootstrap/cheng_v2c` and `v2/examples/unimaker_robot_node.cheng`
- bootstrap tooling/network selfhost artifacts, object-image fixed outputs, real system-link-exec reports, native launch command-dispatch smoke, and fixed-point probes
