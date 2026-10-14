# Leniascope

<img src=images/orbium.bmp height=300 width=300>

This is an implementation of <a href=https://chakazul.github.io/>Bert Chan</a>'s <a href=https://en.wikipedia.org/wiki/Lenia>Lenia</a> in C++ and Win32 API.

Lenia is a continuous generalization of Conway's <a href=https://en.wikipedia.org/wiki/Conway%27s_Game_of_Life>Game of Life</a>, where:

- The on/off state of each pixel is replaced by a value in the real interval [0,1].
- The "neighbour count" of each pixel is replaced by a weighted average of an area around the pixel.
- The discrete change in state is replaced by growth via a difference equation modeling an ODE.

# Laboratory version (this branch)

The modern application is built from **Leniascope.sln**. The original Win32 application remains available through **Starter.sln** for comparison.

## Build and run

Install Visual Studio C++ tools, the optional **MSVC v142 toolset**, and a Windows 10/11 SDK. From PowerShell:

```powershell
.\tools\build.ps1 -Configuration Release -Test
.\x64\Modern\Release\Leniascope.exe
```

Or use a Developer Command Prompt:

```bat
msbuild Leniascope.sln /p:Configuration=Release /p:Platform=x64
x64\CoreTests\CoreTests.exe
x64\Modern\Release\Leniascope.exe
```

`Debug` builds the GUI and CLI without optimization; the solution's numerical test target remains Release. Keep `libfftw3-3.dll` beside the executables. Palette textures and shaders are generated internally; the new UI does not need bitmap icons or a particular working directory. Dear ImGui is vendored at a pinned commit with its MIT license and hashes in `third_party/imgui/VENDOR.txt`.

CMake supports installed current MSVC toolsets as an alternative:

```powershell
cmake -S . -B out -A x64
cmake --build out --config Release --parallel
ctest --test-dir out -C Release --output-on-failure
```

For a checkout inside WSL, prefer the supplied MSBuild solution when building from Windows. Windows CMake also builds it, but its generated incremental rules can emit UNC/case-normalization warnings on a Linux filesystem. Native Windows checkouts do not have that mismatch.

On Linux, CMake builds the portable simulation core and CPU tests only; install a C++17 compiler, CMake, and the FFTW double-precision development library. The desktop renderer and GPU backend require Windows 10/11 and Direct3D feature level 11.0. CI builds Windows and Linux CPU tests; hardware GPU tests are run locally.

### Optional NVIDIA CUDA build

CUDA/cuFFT accelerates the convolution, with a direct GPU-to-GPU display path into the existing Direct3D renderer. It is optional; ordinary builds retain DirectCompute and require no CUDA components. From PowerShell, with Python 3.9+ and the MSVC v142 tools installed:

```powershell
python tools/setup_cuda.py
.\tools\build_cuda.ps1 -Test
.\x64\Cuda\Release\Leniascope.exe
```

The setup script downloads NVIDIA's pinned CUDA 12.9.1 compiler, cuFFT, runtime and dependencies into the ignored `x64/CudaToolkit` directory, verifying the archives against NVIDIA's SHA-256 manifest. It preserves the component licenses and makes no driver, registry or global PATH changes. A compatible NVIDIA driver is required separately. The build copies the required runtime DLLs beside the GUI, CLI and benchmark. Keep those DLLs with the executables. Native code targets the RTX 5070 Ti's architecture, with PTX for compute capability 7.5 and later as a fallback; other GPUs have not been tested locally.

The CUDA build offers **CPU reference**, **DirectCompute**, and **CUDA / cuFFT** in the compute selector. The choice is remembered; `--cuda` overrides it at startup. Existing workspaces continue using their selected CPU/DirectCompute backend. If CUDA solver or display initialization fails, startup reports the failure and tries DirectCompute, then CPU. Switching backends preserves paused fields subject to the selected solver's precision. CUDA uses float32, so long trajectories need not match either other backend.

The CUDA CLI accepts `--backend cuda`. `x64/CudaBench/CudaBench.exe --tests-only` checks CPU/DirectCompute agreement, both rules, rectangular and non-cubic grids, kernel edits, exact checkpoint continuation, display statistics, immutable frames, concurrent rendering and backend transitions. `--benchmark-only` writes CSV to stdout; initialization and diagnostics go to stderr. `python tools/test_cli.py x64/Cuda/Release/LeniascopeCli.exe --cuda` and `python tools/test_workspace.py x64/Cuda/Release/Leniascope.exe x64/Cuda/Release/LeniascopeCli.exe --cuda` exercise CUDA checkpoint/recovery paths end to end.

Windows CMake 3.21+ also supports `-DLENIA_ENABLE_CUDA=ON` with a CUDA 12.8+ toolkit and a compatible host compiler. This was verified with Ninja, CUDA 12.9.1 and MSVC v142. Point `CMAKE_CUDA_COMPILER` at `nvcc.exe` and `CUDAToolkit_ROOT` at the toolkit directory when it is not installed globally. For a WSL checkout, run Ninja/CMake under a temporary drive mapping (`pushd` from a Windows command prompt) because generated link commands cannot use a UNC working directory. The supplied PowerShell/MSBuild script handles the WSL checkout directly.

Local comparison on 2026-10-12, RTX 5070 Ti / Core Ultra 9 285K, driver 610.47, CUDA 12.9.1, MSVC v142 Release:

| Grid | Headless step: DirectCompute → CUDA | Step + display + render: DirectCompute → CUDA |
|---|---:|---:|
| 1024² | 0.290 → 0.098 ms | 0.477 → 0.563 ms |
| 128³ | 0.805 → 0.141 ms | 1.302 → 0.793 ms |
| 256³ | 8.734 → 1.609 ms | 9.751 → 2.898 ms |

These are medians from 100 steps after 10 warm-up steps, seed 42, the default ten-patch recipe, classic rule, `dt=0.1`, and radii 104/26/26 respectively. Display measurements include a 960×720 slice (2D) or default transparent volume (3D), waiting for rendering to complete. Initialization and full-field checkpoint copies are excluded. They measure a serial pipeline, not window FPS; the desktop and an existing Leniascope window remained active. [Raw CSV](benchmarks/cuda-2026-10-12.csv) contains p95 and component timings. `cuda_explicit_device_bytes` counts solver arrays, cuFFT scratch, statistics and allocated display textures, excluding CUDA/driver/library overhead and renderer allocations; blank means unmeasured. At 256³ it reports about 257 MiB headless and 385 MiB with the two display textures used by this run.

CUDA substantially accelerates large 3D fields, but interop overhead makes the complete 2D display path slightly slower in this test. Both GPU backends therefore remain useful. Five-step CPU/CUDA comparisons were within `3.3e-6` maximum absolute error on the tested grids. A 500-step 32³ classic trajectory reached about `0.004` maximum difference: short-step accuracy does not establish long-term equivalence.

Actual-window profiling on the same machine also sustained **120 and 240 steps/s at 256³ with CUDA**. Each run started from the same seed-1 field with 80 patches, radius 26 and default classic parameters. The 1280×820 logical window rendered a transparent volume; CUDA runs recorded roughly 160 presentations/s. The CSV retains 299 measured frames after warm-up. These are short runs, not a sustained thermal/load test. Presentation intervals measure application timing, not photon latency.

| Backend / requested rate | Measured steps/s | Presentation interval median / p95 | Raw profile |
|---|---:|---:|---|
| DirectCompute / 120 | 103.0 | 6.41 / 12.55 ms | [CSV](benchmarks/window-256cube-directcompute-120-2026-10-12.csv) |
| CUDA / 120 | 119.7 | 6.25 / 6.38 ms | [CSV](benchmarks/window-256cube-cuda-120-2026-10-12.csv) |
| CUDA / 240 | 240.0 | 6.25 / 6.40 ms | [CSV](benchmarks/window-256cube-cuda-240-2026-10-12.csv) |

## Using the laboratory

- **Play / Pause**, **Step**, **Reset**, and **New seed** stay in the toolbar. Space toggles playback when not editing text. Reset repeats the displayed seed; New seed advances it.
- DirectCompute GPU compute is selected initially, with a visible CPU fallback if compute initialization fails. Uncheck it to use the double-precision FFTW reference, or use the compute selector in CUDA builds. Rendering still uses Direct3D.
- **2D field** and **3D volume** preserve independent simulation states and view settings. The inspector and viewport keep the same layout.
- **Grid and initialization** exposes patch count, minimum/maximum side length in kernel radii, and the random value range. These controls affect Reset, New seed, and Create grid; changing them does not modify an evolving field. Counts and ranges are independent for 2D and 3D.
- **Placement** adds a single cluster or an isolated pair, with local spacing independent of the size of the grid. **Preview seed (paused)** replaces the current field with the selected initialization and pauses it for inspection.
- Parameter edits take effect through **Apply changes**, at a simulation step boundary. Simulation `dt` and the requested steps/s are independent.
- The live field and Study inspection share the same viewer: a slim footer beneath the image offers a mode selector and **View** popover. Choose **Volume**, **Slice**, or **Surface** (component masks offer **Solid** and **Slice**). Drag a 3D image to orbit and use the wheel to zoom. **View** contains camera/slice controls and cutaway, with **Periodic origin**, **Appearance**, and **Rendering quality** sections. Controls sit outside the image; exported images contain only the rendered field. Each viewer keeps its own camera and settings.
- Transparent volume supports opacity and an opacity floor, compensating opacity for sample distance. **Slice** supports all three axes; **Surface** shows an opaque threshold surface. Display choices never modify the scalar state. Component inspection hides scalar-only settings, and scalar comparisons retain their measured threshold.
- All 20 original palettes remain available, alongside Ocean / gold. A 4,096-entry GPU palette texture replaces per-pixel CPU segment searches. Interpolation is a display approximation, especially near discontinuities.
- **Save state** writes a full 2D or 3D checkpoint. **Load state** pauses on the loaded state. **Export view** writes the rendered viewport as a BMP. The inspector scrolls on smaller windows; measurements include an activity plot and separate compute/display timings.
- **Simulation** and **Study** tabs at the top of the left panel separate evolution controls from analysis. **Study → Use current field** pauses and captures a fixed source without creating a gallery entry. Shape inspection uses the large viewer; behavior plots and batch tables use the **Results** view. The simulation transport is disabled while inspecting a snapshot.
- **Pattern gallery** keeps independent captures with previews, notes, tags and provenance. Browse or filter entries and restore a saved field with its view, or choose **Study this pattern** to inspect it and its existing reports in Study.
- **Study → Behavior** evolves a separate copy of the selected source and records motion, shape mismatch and activity over a chosen duration, with persistent plots and CSV reports.
- **Shape** measures periodic connected components in a saved field at an adjustable threshold. New behavior studies and batches also record these measurements over time.
- Camera, palettes, slice/volume settings, playback rate, compute backend, selected mode, and window size/maximization are remembered between launches. The activity plot starts afresh when a session is reset or loaded.

## Initialization and grid size

The ten-patch default is reproducible and unchanged: each box side is independently uniform from 0.6 to 1.2 times the kernel radius, rounded down to at least one cell, with uniform values in [0,1]. Boxes wrap on the torus; overlaps overwrite earlier values. Larger custom boxes are capped at each grid dimension. Zero patches produces an empty field. Settings are validated, and excessively overlapping jobs are rejected before allocating a new field.

**Placement** separates local proximity from the density of patches over the whole domain:

- **Across grid** retains the original independent uniform placement and coverage estimates.
- **Single cluster** puts the selected number of patch centres uniformly inside a disk (2D) or ball (3D) around the grid centre. **Spread / R** is that disk/ball's radius, in kernel radii. The boxes extend beyond their centres; all other cells start at zero.
- **Isolated pair** places exactly two boxes symmetrically around the grid centre. **Separation / R** controls centre-to-centre distance, not the gap between edges. **Pair angle**, and **Elevation** in 3D, set the direction of their separation; the boxes remain aligned with the grid axes. Positions are rounded to cells. There are no other patches.

For an isolated two-patch experiment on a 1024² grid, choose **Isolated pair**, leave the side/value ranges as desired, and start with separation **1.5 R**. Use **Preview seed (paused)**, then Play. Adjust separation and Reset while holding the seed fixed to compare the same patch sizes and random contents at different distances; **New seed** changes them. A **Single cluster** of three or four patches with a small spread provides another controlled starting point. These controls do not guarantee an Orbium or a conjoined pair will form.

Cluster/pair resizing retains the patch count, even when **Keep seed coverage on resize** is checked; that checkbox is disabled for these layouts. At a fixed radius, enlarging an even-sized grid translates the same local seed to the new centre and adds empty space around it. **Scale radius with grid** still scales both box sizes and local spacing. The UI warns when the chosen spread and box sizes may reach periodic edges; reduce them or enlarge the grid to keep a surrounding empty region. Periodic copies can still interact during evolution. Uniform-coverage estimates are hidden for these correlated layouts.

With radius 26 and ten patches, expected geometric coverage is about **5.58% at 128³** but **0.71% at 256³**. Increasing the grid previously retained both radius and patch count, so it enlarged the domain without compensating seed density. This is a change in the experiment, not merely finer resolution; it does not by itself establish which morphology will survive.

**Keep seed coverage on resize** is enabled by default. Create grid then adjusts the count to approximately preserve expected coverage, accounting for patch overlap, rounding, and radius changes; the preview shows the resulting radius/count before creation. For 128³ → 256³ at radius 26, ten patches become 80. Disable it to keep a fixed count. **Scale radius with grid** is a separate, initially unchecked option; doubling the radius to 52 grows the patches too, so roughly ten patches suffice. Neither option resamples the current field or guarantees identical dynamics across resolutions.

Coverage and expected mean are statistical estimates for the selected recipe, not measurements of the evolved field or predictions of survival. The estimate is `1 - (1 - E[patch volume]/grid volume)^count`; expected mean multiplies this by the midpoint of the value range. Equal size or value endpoints are supported. Reset uses the current applied model parameters; if the kernel radius has changed since initialization, the UI also reports the original seeding radius. Pending seed controls survive switching modes; opening a checkpoint restores its recorded recipe when available.

CLI experiments accept `--patches`, `--patch-min`, `--patch-max` (side/radius), `--value-min`, and `--value-max`. They apply only to newly generated fields and cannot be combined with `--input`, which resumes an actual checkpoint. For example:

```powershell
.\x64\Modern\Release\LeniascopeCli.exe run --size 256 --radius 26 --seed 7 --patches 80 --patch-min 0.6 --patch-max 1.2 --value-min 0 --value-max 1 --steps 100 --backend gpu --output dense.lenia
```

`--layout uniform|cluster|pair`, `--spread` (cluster radius or pair separation in R), `--angle` and `--elevation` expose placement to repeatable experiments. Pair mode defaults to two patches and rejects any other count. For example:

```powershell
.\x64\Cuda\Release\LeniascopeCli.exe run --dimensions 2 --size 1024 --radius 103 --seed 7 --layout pair --spread 1.5 --angle 0 --steps 0 --backend cuda --output isolated-pair.lenia
```

## Pattern gallery

Open **Pattern gallery** in the inspector's **Experiment** section, then choose **Add current field**. Capture pauses evolution at a step boundary. The preview is rendered from that exact captured field, using the current camera, palette and periodic view origin. Add a title, tags and notes, then **Save entry**. Cancel leaves the simulation paused without creating an entry.

The gallery starts empty. To import an existing checkpoint, load it in the laboratory and then capture it. Search title/tags/notes or filter by dimension, select an entry to inspect its preview and provenance, then use **Load field + view**. Reloading restores the actual field, parameters, counters and view, paused; it does not regenerate from a seed. The inactive simulation session is preserved. Gallery and checkpoint loading automatically select CPU when the saved values cannot be represented exactly by the current float32 GPU backend. Subsequent backend changes remain explicit numerical choices.

Loading a gallery pattern or checkpoint, or applying an edited field, also sets that mode's starting field. **Reset** restores its exact cell values and starting counters while retaining the currently applied model parameters. For example, load an Orbium, evolve it, reduce Radius and apply the change, then Reset to retry the original shape with the smaller kernel. Each dimension retains its own starting field across mode/backend switches and workspace reopening. **New seed**, **Preview seed**, and **Create grid** replace the starting field with a random initialization; Reset then uses the current seed controls as before. Reset preserves the current playback/paused state. To restore the saved model parameters and view as well, reload the gallery entry.

Entries initially have the **Candidate** label. **Reference (your assessment)** is a manual label, not an automatic claim about stability or species. **Observed simulation time** records your reported observation duration; zero means unrecorded. It is separate from the field's simulation age. Displayed mean/minimum/maximum are measurements of the captured scalar field; there is no automatic morphology or stability classifier. **Edit notes and labels** changes only annotations, preserving the checkpoint and preview.

Provenance records the current parameters/backend and the initial seed recipe when known. It does not record the entire history of parameter or backend changes made before capture; use notes for those observations.

Each workspace has a `gallery` directory, normally `%LOCALAPPDATA%\Leniascope\Laboratory\gallery`; an explicit `--workspace` keeps its gallery there instead. Each entry is a separate directory containing `state.lenia`, `preview.bmp`, and bounded versioned text metadata `entry.txt`. Copy the entire entry directory to back it up or transfer it into another gallery, then Refresh. Duplicate titles do not overwrite fields. There is no automatic deletion or pruning. Gallery entries are independent of the two rotating recovery slots.

Use a native Windows directory for large experimental workspaces when possible. A 256³ checkpoint contains about 128 MiB of field data, and accessing it through a WSL shared path can make save, verification and recovery substantially slower.

Saving writes a private staging directory and commits it with one rename. Interrupted staging directories are ignored; malformed entries are reported without hiding valid ones. Checksums detect accidental checkpoint/preview corruption, and reload cross-checks the stored provenance and scalar measurements. These are integrity checks, not cryptographic authentication or guarantees against hardware failure. A damaged preview does not prevent loading an intact field. Only the workspace-owning window can write gallery entries. Catalog and file work runs in the background; capture/readback and preview creation can briefly delay a large-grid operation. Closing waits for accepted gallery work before taking the final workspace snapshot and reports write failures.

The portable `src/gallery.*` store is separate from the ImGui interface in `src/gallery_panel.*`; `src/batch.*` provides the batch engine and store, with controls in `src/study_batch.cpp`. `StudyPanel` owns the source and background jobs independently of `GalleryPanel`, with behavior and shape UI in `src/study_behavior.cpp` and `src/study_shape.cpp`. Live numerical stepping remains in the simulation worker. `python tools/test_gallery.py <GUI> <CLI> --cuda` exercises the real GUI capture/save/browse/reload, both saved-pattern and direct-current-field studies, batches, outcome labels and endpoint loading for CPU, DirectCompute and CUDA in both dimensions, checking full checkpoint bytes, camera settings, the inactive session, Reset state, persisted reports, and that current-field studies do not add gallery entries. Omit `--cuda` for a standard build. The application's `--gallery-smoke` test mode requires an explicit isolated `--workspace`.

## Study

Open **Study** at the top of the left panel, then **Use current field**. Entering Study pauses playback; capture pins the field at a step boundary and retains its camera as an independent study view. Switching back to Simulation retains the field and its existing Reset state. Capturing or measuring does not load a new starting field. To inspect a gallery pattern instead, choose **Study this pattern** in the gallery; this opens its stored source and reports without replacing the simulation.

Current-field snapshots are retained in the workspace's separate `studies/<source>/` archive and can be reopened through **Recent study snapshots**. This reuses the atomic checkpoint/metadata format but does not add patterns to the gallery. Each capture keeps its exact field, preview and source parameters; reports and batches live beneath that source. A 256³ source checkpoint takes about 128 MiB. Snapshots are not pruned automatically. Existing gallery reports stay under their original gallery entry and remain accessible through Study. A read-only window can inspect stored sources but cannot capture or save new runs.

Choose **Shape**, **Behavior**, or **Batches** in the study tool selector. Shape membership appears in the full-size main viewer. Behavior and Batches offer **Field** and **Results** views, with wide plots/tables and independently scrolling settings. View origin, orbit and other display settings remain in the shared viewer's **View** menu.

### Shape measurements

Choose **Shape**, set the activity threshold, then **Measure shape**. This reads the saved scalar values without stepping the simulation. Cells are active when their value is **strictly greater** than the threshold. Connectivity uses four edge neighbors in 2D or six face neighbors in 3D, including neighbors across periodic boundaries; diagonal/corner-only contacts remain separate. The threshold is independent of the renderer's opacity/surface controls.

The full-size viewer colors those exact active cells by component. Select a row in the size-ordered table to isolate that component; **Show all components** restores the full mask. Each row lists its cell count, percentage of active cells, and periodic winding directions. IDs/colors are local to one measurement and may change when the threshold changes. Colors help locate regions; isolation is the definitive membership check. The integer label texture is not interpolated or re-thresholded on the GPU; 3D rays traverse the crossed voxels, including thin bridges. This intentionally looks more blocky than the scalar-field renderer. Small components may still be smaller than a screen pixel.

Drag to rotate, use the wheel to zoom, or choose **Slice** in the viewer footer to inspect hollow interiors. The same **View** popover as the live viewer provides cutaway and **Periodic origin**. These affect visibility only: a cut or slice can separate the visible pieces of one connected component, and a single component can appear on opposite faces of the displayed cube. **Scalar field** compares the full saved field using the same camera and usual interpolated color/opacity; it does not isolate the selected component. Its volume opacity uses the measured threshold, while scalar slices/2D views show all values. Inspection never changes the live simulation or its Reset state. Changing the threshold input leaves the previous measurement visible, explicitly labeled, until you measure again.

The measured components belong to the region of **active material**, not its boundary surfaces. A thick hollow shell can be one material component with separate inner and outer boundary surfaces. A toroidal shell and a solid torus have different topology even though each can be one connected component. Periodic winding detects paths around the ambient domain; it is not a count of intrinsic handles, genus, knots, or links. Surface genus would require a consistent periodic surface construction and manifold checks first. Knot/link classification requires further information about the embedding, beyond component counts or genus.

The measurements report active-cell count, connected-component count, the largest component's cell count and its share of **active cells**, plus the number of components containing a **periodic loop** and the directions those loops wind around. A blob split by a display edge is still one component. Crossing that edge does not by itself imply a periodic loop: the code requires a closed path whose lift to the periodically repeated grid returns to a different image of the starting cell. A local annulus or hollow shell can therefore have no periodic loops. Direction flags are the union over all components; they do not describe the number of independent cycles, cavities, knots, or detailed topology.

Try several thresholds: a low-amplitude bridge may join two otherwise separate regions. These measurements describe thresholded geometry, not verified organism counts or automatic sphere/tube/Orbium labels. The Shape tool measures the pinned source exactly; study and batch measurements use the solver's actual field (including recorded initial float32 rounding on GPU runs).

New **Behavior** reports include a component-count plot and final shape statistics under **Connected components and periodic loops**. **Batches** shows component counts in its **Parts** column and details under **Final shape measurements**. The existing activity threshold controls all these sampled masks. CSV exports append `active_cells`, `components`, `largest_component_cells`, `largest_active_fraction`, `winding_components`, and X/Y/Z winding flags. The largest share is empty for an empty mask. Behavior report schema 2 stores the counts at every sample. Schema 1 remains readable and its missing measurements display as unavailable and export as empty cells; existing reports are not recomputed or rewritten when browsed.

The portable analysis is in `src/morphology.*`. It runs on the study/background thread, using at most 16 bytes per grid cell of additional traversal storage (256 MiB at 256³). Interactive inspection also allocates a cell-label map, a per-component table, and display textures; studies retain only the aggregate counts. Like the existing FFT alignment measurements, it adds CPU work at sampling times even with CUDA evolution. The CLI can inspect any checkpoint without evolution:

```powershell
.\LeniascopeCli.exe shape pattern.lenia --threshold 0.01
```

### Behavior studies

In Study, choose **Behavior**, choose the number of steps, sampling interval and backend, then **Run study**. The default measures 1,000 steps at intervals of 20. The saved field, annotations and current simulation remain unchanged: a background job constructs its own solver from the saved checkpoint. Backend selection is explicit and failures are reported without switching arithmetic. Running a study can compete with live playback for GPU/CPU resources; pausing playback is useful for large fields. FFTW computes translation measurements on the CPU even when evolution uses a GPU.

**Stop study (keep results)** records the completed endpoint. Closing the application requests the same stop and waits for the current step/measurement and report save before normal workspace recovery. Saving failures retain the report for retry; discarding it is an explicit action. Studies require the window that owns the workspace. The plots and report selector reappear in **Results** when the source is reopened; multiple runs are retained independently.

Each measurement compares the evolved field `A_t` with its starting reference `A_0`:

- **Mean activity** is the mean scalar value. **Fraction above activity threshold** counts cells strictly above the selected threshold; that threshold changes the measurement only.
- **Raw RMS** measures pointwise difference. **Aligned RMS** minimizes that difference over integer periodic translations, using FFT cross-correlation. The reported displacement compares `A_t(x + d)` with `A_0(x)`.
- **Relative mismatch** divides aligned RMS by the starting field's RMS. This prevents a large empty background from making a localized organism's deformation look artificially small. It is undefined for an empty reference.

The reference is the solver's initial snapshot. Converting a CPU checkpoint to a float32 GPU solver can round it; **Initial rounding RMS** records that separate change. Empty/near-constant fields and tied correlation peaks do not receive a unique displacement. Shifts are modulo the grid and describe the whole field, not individual organisms; there is no unwrapped path or velocity estimate. Rotation and subcell shifts are not aligned. Integer alignment can itself cause oscillating residuals as a translating shape moves between cells, so oscillations do not establish an internal pulsation period. Low sampled mismatch is evidence of a return over that duration, not proof of periodicity or stability. Short periods can be missed by a coarse sampling interval. The graphs use sample index horizontally, with exact step/time in hover text, since the last interval can be shorter.

Plots label their vertical ranges and show starting, final and peak values as percentages. A 1% relative mismatch means the RMS difference is 1% of the reference RMS, not that 1% of cells changed. Default vertical axes start at zero and extend to at least 100%; mismatch may exceed 100%, in which case the axis expands to include it. **Magnify variations (independent Y scales)** stretches each plot to its observed range, making small changes visible. Read the displayed ranges before comparing magnified plots. This also applies to mean activity and the fraction above threshold: a full-height line can otherwise exaggerate a tiny variation. Mismatch is measured against the original field, not the previous sample, so a small steady drift can accumulate into a rising line without implying accelerating change.

Up to 256 measured lags plus the starting sample are retained. The endpoint is included even when it falls between intervals. Each report is saved under `studies/<source>/studies/<report>/` for a current-field snapshot, or `gallery/<entry>/studies/<report>/` for a gallery source as `report.txt` (versioned parameters, source checksum, numerical backend and measurements) and `samples.csv` (one row per sample). **Open report folder** exposes both files. Undefined relative mismatch or displacement fields are blank in CSV. Copy the whole entry directory to back up its reports too. Writes use a staging directory and rename; existing reports and source fields are never overwritten. Invalid reports are listed separately without hiding valid ones.

The CLI provides the same measurements for any checkpoint, without requiring a gallery:

```powershell
.\x64\Cuda\Release\LeniascopeCli.exe study orbium.lenia --steps 4000 --sample-every 20 --backend cuda --threshold 0.01 --output orbium-study
```

The output directory must be new. It receives the report and CSV; CSV is also written to stdout. The default backend is CPU. Report wall time includes sampling and correlation but excludes solver construction and input loading. The scalar measurements live in portable `src/behavior.*`; they do not feed back into evolution.

## Field editor (2D)

Choose **Edit 2D field** under **Experiment**. Capture pauses the simulation at a step boundary and opens a separate draft. Drag a rectangle, then **Copy selection** (Ctrl+C). Switch to placement, move over the field to preview, and click to stamp; the numeric centre coordinates and clockwise angle let you position copies precisely. **Keep selection only** isolates an organism, **Erase selection** removes a region, and **Clear field** gives an empty starting field. The clipboard survives closing/reopening the editor during the same application session, so it can also transfer a selection between loaded 2D fields.

For a controlled Orbium pair: load a saved Orbium from the gallery, select it with some empty margin, keep only that selection, and copy it. Place a second copy at a chosen separation and rotation. **Maximum** overlap keeps the higher of existing and pasted values; **Replace** overwrites the selected footprint, including copied zeros. **Make random patch** creates a rectangle or ellipse with a chosen size, value range and repeatable seed, ready for placement. **Undo/Redo** (Ctrl+Z / Ctrl+Y) restore exact draft values, retaining up to 32 operations within 128 MiB of value history.

Selections and placements wrap across the periodic boundaries. Enter a region origin and size to select across an edge, or move **View origin** to bring the object together on screen. The editor shows the whole field with a cell-aligned origin; its view changes do not move the field or alter the main camera. Placement centres snap to the cell lattice: an even-width footprint has a half-integer centre. Coordinates count cells from zero. Copies preserve their cell size without rescaling to a different destination kernel radius.

Right-angle rotations permute the cells exactly. Other angles use inverse-mapped bilinear interpolation from the original clipboard, with zero extension and interpolated coverage at its edges; repeated angle adjustments do not repeatedly resample the result. This interpolation is not mass-conserving and may alter a delicate organism. Rotated footprints larger than one domain are rejected. The editor changes scalar activity values, independently of the display colour map. CPU drafts retain float64 values; GPU drafts round modified cells to float32, matching the current solver. Undo preserves original values exactly in either case.

**Apply field** replaces the paused 2D session and starts a new experiment at step/time zero; **Discard draft** leaves the original field paused. The separate 3D session is retained. Unapplied drafts are not autosaved. After applying, use Play to evolve or save the new field to the gallery before evolving it. Edited checkpoints, recovery saves and gallery entries retain a bounded construction log with parent-field fingerprints, model/counter/backend details, copied source regions, patch seeds, rotations and placements. These logs identify the construction but are not a standalone replay archive of every copied source; the saved field is authoritative. Subsequent evolution retains the log. The log is limited to 16 KiB; reopening a long construction retains its recent operations and replaces older history with a fingerprint and an explicit compaction marker. Save intermediate fields to the gallery when their full construction history matters.

The portable `src/field_edit.*` code owns selection, patch geometry and reversible edits; `src/editor_panel.*` owns the paused UI and preview. `python tools/test_editor.py <GUI> <CLI> --cuda` exercises discard, apply, wrapped placement, rotation, undo/redo, restart and gallery persistence on CPU, DirectCompute and CUDA, in isolated workspaces. Omit `--cuda` for a standard build. Its `--editor-smoke` application mode requires an explicit `--workspace`.

## Workspace recovery

The **Workspace** section controls automatic recovery. By default, both initialized simulation sessions are saved every 30 seconds and when the window closes. Reopening restores their actual fields, parameters, seeds, counters, and simulation times **paused**, together with any loaded/edited starting fields used by Reset. These immutable starting fields are stored separately from the evolving fields. Recovery manifests use version 2 when starting fields are present and still read version 1; older workspaces have no saved starting fields until a pattern is loaded or an edit applied. **Save workspace now** requests an immediate recovery checkpoint; **Save state** still creates a separate experiment file containing only the current field, which becomes the starting field when loaded. Turning recovery off preserves display preferences but starts fresh fields on the next launch.

Files live in `%LOCALAPPDATA%\Leniascope\Laboratory`, independently of the repository and current working directory. Use `Leniascope.exe --workspace <directory>` for an isolated workspace. An exclusive file lock prevents two windows from overwriting the same workspace; additional windows can read it, but their automatic saving is disabled. Manual experiment saves remain available.

Recovery captures are queued on the simulation worker at a step boundary, independently of display textures. Full GPU readback happens only on demand for checkpoints or CPU transitions; it can briefly delay evolution on a large grid. Recovery writes run on a separate thread using immutable snapshots. Two alternating slots retain a previous complete recovery. Each slot is committed only after both available fields are written; checksums detect accidental field corruption. If the latest slot is incomplete or damaged, loading tries the previous complete slot. Close waits for accepted simulation commands and the final background save. If that save fails, the application offers retry, keeping the paused window open, or closing without a new recovery. These are application-crash/interrupted-write safeguards, not a guarantee against storage hardware failure or loss of unsaved work between checkpoints.

## Display profiling

Build `tests/PipelineBench.vcxproj` with Release|x64 (or the CMake `PipelineBench` target), then run `x64/PipelineBench/PipelineBench.exe`. It reports CSV p50/p95 wall timings for a step, display preparation, CPU measurements, upload submission, render completion, and their total. The compute column uses GPU timestamps. Each path starts from the same seed, uses a 960×720 volume viewport, and measures 60 steps after 5 warm-up steps. Render completion includes any pending asynchronous upload; these are serial pipeline measurements, not interactive FPS.

On the RTX 5070 Ti / Core Ultra 9 285K (2026-10-11), median total time fell from **12.75 to 1.48 ms at 128³**, and **105.66 to 11.89 ms at 256³**. At 256³, p95 total fell from **117.39 to 12.97 ms**. Numerical evolution is unchanged. Statistics are reduced in float32 on the GPU and are diagnostic approximations; saved field values remain exact snapshots of the selected solver.

`Leniascope.exe --profile` runs the actual window for 330 running frames, exports its own rendered buffers, gracefully closes, and writes `frame-profile.csv` in the current directory, discarding 30 warm-up frames. Like `--smoke`, it uses a fresh isolated session unless you explicitly pass `--workspace <directory>`. Use a disposable workspace to select a different grid/rate; that workspace is advanced and saved by the profiling run. Keep the test window visible. `--profile --cpu-transfer` measures the earlier display path. The CSV records presentation intervals, compute duration, publication wall time, draw submission wall time, Present wall time, displayed step, and backend. Draw/Present submission durations are not GPU render timestamps or display photon latency.

In those actual-window DirectCompute runs, 128³ sustained 60 and 120 steps/s; 256³ sustained 60 steps/s and reached about 81 steps/s when 120 was requested. Viewport size, volume contents, camera, sample spacing, refresh rate, and other GPU work affect these numbers. The remaining large-grid cost was primarily FFT computation, motivating the optional CUDA/cuFFT backend described above.

`RenderTests` checks pixel-for-pixel agreement between the two display paths, GPU statistics, immutable/bounded display leases, concurrent rendering, and checkpoint/backend/restore transitions. `python tools/test_workspace.py <GUI> <CLI> --gpu` exercises actual GPU-backed GUI restart and byte-exact field recovery; omit `--gpu` for CPU recovery.

## Architecture and numerical behavior

`src/simulation.*` contains validated field configuration, deterministic SplitMix64 initialization, periodic kernels, the double-precision FFTW solver, checkpoints, and recurrence measurements. It contains no Win32 dependency. Array order is `(z * height + y) * width + x`; FFT dimensions are explicitly `(depth, height, width)`. This corrects the legacy solver's transposed dimensions on rectangular/non-cubic grids. Radius is limited to half the shortest active dimension to avoid ambiguous wrapped kernel construction.

`src/gpu_solver.*` implements a separable complex radix-2 FFT and the update rule in DirectCompute float32. Each axis transforms independent rows using group-shared memory. State and FFT intermediates stay on the GPU between steps. Completion fences keep the worker from polling on coarse Windows timers. GPU stage timing is currently reported as an overall compute duration; CPU timings separate forward FFT, multiplication, inverse FFT, and update.

`src/cuda_solver.*` implements the same rules with cuFFT real-to-complex/complex-to-real transforms, storing only the nonredundant spectrum. Plans and their shared scratch space persist for the solver's lifetime. CUDA events report the individual compute stages and provide blocking completion waits. Display textures are registered with CUDA and populated without a full CPU field transfer. CUDA's internal Direct3D calls require `ID3D11Multithread` protection in addition to the application's submission mutex; the CUDA display path enables it before registering resources.

`src/runner.*` owns mutable simulation states and solver resources on one worker. UI commands are bounded and processed between steps. Every completed step is offered to the display; a slower viewer can skip intermediate images without dropping numerical steps. A [high-resolution Windows waitable timer](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createwaitabletimerexw) paces the worker against steady-clock deadlines, with a separate event for immediate command/shutdown wakeups and an ordinary timer fallback. This avoids both the former 30 Hz publication cap and accumulated sleep drift.

GPU simulation and rendering share one device. A recursive mutex serializes immediate-context command batches, following [Direct3D's synchronization requirements](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-intro); completion waits release it. The worker copies the field to a pool of at most three immutable GPU display textures per session, and calculates mean/min/max in two GPU reductions. Only 16 bytes of statistics cross to the CPU for each display publication. Leases keep an older displayed texture intact until its readers release it; a full pool skips publication, not evolution. Display-frame metadata intentionally has no CPU field values. The window uses a flip-model swap chain and a [frame-latency wait handle](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_3/nf-dxgi1_3-idxgiswapchain2-getframelatencywaitableobject), waiting outside the context lock so vertical refresh does not stall simulation. `--cpu-transfer` retains the previous readback/upload display path for comparison or diagnosis. CPU simulation continues to publish complete CPU snapshots.

`src/renderer.*` reuses the volume, palette, render-target, and graphics resources. `src/viewer.*` owns the shared image interaction, bounding box, mode selector and view popover used by the live field and Study Shape inspection; view settings remain caller-owned. The application shell in `src/application.cpp` handles window events and simulation controls. The numerical core can run through the CLI without opening a window.

`ViewerTests` exercises actual ImGui input across two viewer instances using a software Direct3D device: orbit/zoom isolation, wheel ownership inside a scrolling parent, independent popovers, Escape dismissal, slice selection, persistence limits and the fixed gallery-comparison threshold. CMake runs it on Windows via CTest, without a physical GPU or desktop window.

`src/workspace.*` contains portable preference serialization, recovery validation, and the background writer; `src/desktop_workspace.*` resolves the Windows location and owns its file lock. `src/view_settings.h` has no graphics dependency. Worker commands execute in order; a failed command pauses evolution and reports the error while allowing later accepted commands to run. Manual steps execute before following mode/save commands. Paused worker updates wake the UI without waiting for its idle timeout.

Classic Lenia remains `a_next = clamp(a + dt * (2*exp(-0.5*((K*a-mu)/sigma)^2)-1), 0, 1)`. The radial kernel uses a fixed Gaussian centered at normalized radius 0.5 with width 0.15, then is normalized to unit sum. `mu` and `sigma` control the growth function, not that radial Gaussian. Random patch placement now scales with the radius and wraps safely; it intentionally does not reproduce the old clock-seeded `rand()` initialization.

Float32 GPU trajectories can diverge from float64 CPU trajectories over many steps, particularly near sensitive patterns. Use CPU double for the reference, compare short trajectories with tolerances, and test timestep and spatial-resolution convergence before making mathematical claims. Display RGB values are never fed back into evolution.

## Experiments

The **Study → Batches** tool runs controlled comparisons in the background. Capture or open a source, choose a parameter and enter comma-separated values, set the observation horizon and backend, then click **Run batch**. It supports growth mean/width and integer kernel radius. **Use seed recipe** additionally enables pair separation (or cluster radius) and pair angle, with consecutive seeds reused across every parameter value. The saved recipe must be available; this mode generates fresh initial conditions rather than reconstructing the saved organism. Patch sizes use each run's kernel radius. Exact-field mode keeps all starting cell values fixed, including when R changes; it does not rescale the organism.

Each run saves its actual initial and final checkpoints, the behavior report and time series. A batch also retains the source checkpoint, its plan, checksums, and `summary.csv`. Runs are sequential with at most 256 per batch; the UI estimates field storage before starting. The live simulation and original gallery entry are unchanged. Stop keeps completed runs and the current partial endpoint. Closing the app requests the same stop. A crash or solver/write failure leaves the batch marked incomplete and preserves already committed runs; automatic continuation is not implemented. The explicit backend never silently falls back to another arithmetic implementation. GPU initial rounding is recorded.

Select a result row to **Load initial field** or **Load final field**, paused, as the new Reset starting field. You can then evolve it or add it to the gallery. **Save outcome label** records your assessment (for example spheres, tubes, transient, or unknown) at that run's stated horizon. No morphology classifier is inferred from mean activity or mismatch. **Open batch folder / CSV** exposes the files; results and labels also reload when the study source is reopened. Batches live under `studies/<source>/batches/` or `gallery/<entry-id>/batches/`, separately from single behavior reports. The same engine is available through the CLI:

```powershell
# Replay exactly the same saved field at three growth means (new output directory required)
.\LeniascopeCli.exe batch organism.lenia --parameter mu --values 0.14,0.15,0.16 --steps 1000 --sample-every 100 --backend cuda --output organism-means

# Use a checkpoint carrying a pair seed recipe: three separations, ten paired seeds, thirty runs
.\LeniascopeCli.exe batch pair-seed.lenia --parameter spread --values 1,1.5,2 --seeds 10 --first-seed 216 --steps 1000 --sample-every 100 --backend cuda --output pair-separations
```

The batch horizon is measured in additional steps at the source's unchanged timestep; the CSV includes elapsed simulation time and marks partial runs. Separation is the requested placement distance in units of R, with periodic wrapping, not a measured final distance. Saved fields are the ground truth when patches overlap or wrap. Labels are editable independently of those fields; the CSV is an export, and labels should be edited in the app.

```powershell
# Seeded 3D experiment, periodic checkpoints, and measurements
.\x64\Modern\Release\LeniascopeCli.exe run --dimensions 3 --size 128 --radius 26 --seed 7 --steps 1000 --backend gpu --sample-every 10 --checkpoint-every 100 --csv run.csv --output final.lenia

# Repeatable parameter sweep from the same initial state for each mean
.\x64\Modern\Release\LeniascopeCli.exe run --dimensions 2 --size 256 --radius 26 --seed 7 --steps 500 --sweep-mu 0.13,0.14,0.15 --csv sweep.csv --output sweep.lenia

# Resume an experiment with a different timestep
.\x64\Modern\Release\LeniascopeCli.exe run --input final.lenia --dt 0.05 --steps 100 --backend cpu --output continued.lenia

# Best integer spatial translation and recurrence RMS error
.\x64\Modern\Release\LeniascopeCli.exe compare first.lenia second.lenia

# Evolve one saved field over the same duration at dt, dt/2, dt/4, and dt/8
.\x64\Modern\Release\LeniascopeCli.exe converge first.lenia --steps 20 --levels 4 > convergence.csv
```

Sweep filenames receive `.run-N` suffixes. A seed identifies the initialization algorithm and its input; checkpoints contain the actual field. Version 4 stores dimensions, parameters, rule, smoothing sharpness, seed, step count, simulation time, solver identifier, double-valued state, and an optional initialization recipe (count, size/value ranges, original radius, layout, spread and pair direction). CSV schema 4 records the same recipe; `seed_layout` is 0 for uniform, 1 for cluster, 2 for pair. Version 2/3 checkpoints remain readable; unavailable seed provenance is marked unknown and older recipes retain uniform placement. Workspace preferences are version 4 and still read versions 1–3. Unedited fields still use checkpoint version 4 and gallery metadata version 2. Edited fields use checkpoint version 5 and gallery metadata version 3, adding the construction log; this build reads all previous versions, including original checkpoints and previews. Existing gallery entries are not rewritten when browsed or loaded. Older executables cannot read the new formats. The recipe is not a history of subsequent parameter edits. GPU values are promoted from float32 when saved, without adding precision. Writes use a temporary sibling and rename after a successful close. The binary format assumes little-endian IEEE-754, as on the supported Windows/x86-64 and tested Linux/x86-64 systems. Valid legacy 2D `Leniav1` files can be imported; ambiguous legacy 3D payloads are rejected.

`compare` computes periodic cross-correlation with FFTW, then verifies the best integer translation using an RMS residual. Its convention is `second(x + d)` compared with `first(x)`. It reports the time difference, translation, and residual; this is a candidate-recurrence diagnostic, not a proof of a relative periodic orbit. Constant or symmetric fields can have non-unique best translations. Subcell shifts, continuation, stability analysis, and certified orbit finding remain research work.

`converge` uses CPU double for every run, starting from the exact same checkpoint without modifying it. The simulated duration is the saved timestep times `--steps`; finer runs take correspondingly more steps. CSV rows report timestep, step count, duration, mean activity, and pointwise RMS/maximum differences from the finest run. The finest row therefore has zero residual **by definition**, not zero numerical error. Repeat with finer levels and appropriate durations, particularly for clipping events or sensitive patterns. GPU checkpoints are usable starting fields, but their stored float32 values do not gain precision by promotion to double. This command refines time only; spatial refinement needs a consistently sampled physical field and scaled kernel radius.

The explicitly experimental **Smooth reaction** rule implements the C-infinity bounded reaction law

```text
g(u) = 2 exp(-0.5 ((u-mu)/sigma)^2) - 1
b(u) = softplus(sharpness*g(u)) / sharpness
d(u) = softplus(-sharpness*g(u)) / sharpness
da/dt = b(K*a) (1-a) - d(K*a) a
```

It uses exponential Euler with the neighborhood frozen during each step, preserving [0,1]. This is a different model with no hard clipping; empty regions can nucleate. It is not claimed to converge to classic Lenia as sharpness grows. The paper [Existence of Life in Lenia](https://arxiv.org/abs/2203.14390) provides a starting point for research on the original nonsmooth flow. GPU volume rendering follows the usual [ray-marching and transfer-function approach](https://developer.nvidia.com/gpugems/gpugems3/part-v-physics-simulation/chapter-30-real-time-simulation-and-rendering-3d-fluids).

## Verification and performance

`CoreTests` checks independent direct convolution on square, rectangular, and non-cubic grids; seeded repeatability; kernel normalization; 3D checkpoint round trips/replacement; invalid input; torus shift recovery; smooth-rule bounds and small-step consistency; CPU/GPU agreement; and worker lifecycle/state ownership. The baseline target additionally verifies five-step agreement with the original solver on square and cubic grids. `--cpu-only` skips hardware-dependent tests. `tests/RenderTests.vcxproj` checks slices, volumes, surfaces, cutaways, sampling-opacity consistency, rectangular resizing, and every palette. `Leniascope.exe --smoke` saves the application's own rendered interface and viewport in its working directory and exits; `tools/convert-preview.py` converts these BMPs to PNG without external packages.

The extended tests cover corrupt/interrupted recovery, write failures, preference validation, two-window locking on Windows, command ordering after failures, both restored sessions, and the animation publication cadence. Numerical checks refine time and 16/32/64-cell spatial grids for the same smooth periodic field with a fixed physical kernel radius, in both dimensions and both rules. They also run 500-step boundedness checks and report CPU/GPU drift. On one local seeded 2D classic case, RMS drift rose from about `1.7e-6` at step 100 to `3.8e-4` at step 500; this illustrates why short-step agreement does not imply identical long trajectories.

`tools/test_cli.py <CLI executable>` checks command-line experiments and timestep studies end to end and runs in Windows CI. `tools/test_workspace.py <GUI executable> <CLI executable>` opens and closes the actual GUI twice with an isolated temporary workspace, verifying both paused fields and view settings survive exactly. It requires a local graphics adapter. Ordinary `--smoke` does not access personal workspace files; adding `--workspace <directory>` exercises persistence and the normal close path in that directory.

Run `tools/benchmark.ps1` for matched seeded CPU/GPU measurements. Initial 30-step Release measurements on a Core Ultra 9 285K / RTX 5070 Ti, with a snapshot and CSV sample every 10 steps:

| Grid | CPU double wall steps/s | GPU float wall steps/s | CPU compute ms/step | GPU compute ms/step |
|---|---:|---:|---:|---:|
| 1024 x 1024 | 88 | 1252 | 10.94 | 0.234 |
| 128 x 128 x 128 | 46 | 555 | 20.98 | 0.720 |

These are short local headless measurements, not universal speedups or display frame rates. Planning, shader compilation, and initialization precede the timed region; snapshot/CSV work is included in wall throughput. The original baseline used a different dense initial state: approximately 20.66/40.62 ms per step for 2D/3D and 6.62/1.58 ms for its color loops. It demonstrated a 0.1512 one-step error on a 16x8 grid. The new CPU direct-convolution checks were below 1e-12, and the tested five-step GPU comparisons below 3e-6.

## Research direction and next work

The gallery stores actual fields with previews, model parameters, provenance and numerical backend, and attaches repeatable behavior measurements over a specified duration. Batches support exact-field parameter comparisons and repeated seed-recipe sweeps, saved endpoints and manual outcome labels. Periodic component counts and winding measurements are now available for saved fields and sampled trajectories. A seed recipe alone cannot stand in for a discovered organism. Candidate/reference labels remain manual; they are not proofs of stability. Next, use the saved single Orbium, separated pairs and transient conjoined pairs to compare trajectories and refine measurement intervals. The items below are research plans, not implemented classifiers or established explanations.

The first 2D field editor now provides paused selection, copy, rotation, placement, shaped random patches, undo and saved construction history. Constructed pairs can be compared at controlled separations and orientations while retaining their original gallery entries. Revolving a 2D pattern around a chosen movement axis into a 3D field is a later extension: its axis, interpolation, spatial scale and 3D model parameters need explicit choices. That construction supplies an initial condition, without assuming the 2D dynamics or organism will survive in 3D.

Orbium work should distinguish **existence**, **stability**, and **accessibility from a particular seeding distribution**. Failure to find an organism by random seeding does not settle the first two. Compare classic and smooth models from the same saved organisms, measure motion and recurrence, and track the activity of expanding fronts. The current Smooth reaction is a different birth/death law, not a controlled approximation to classic clipping; a controlled smoothing experiment needs a separately defined family and convergence checks. Likewise, carrying the same radial kernel profile into 3D changes the weight of spherical shells. Kernel geometry and discretization should be explicit experimental variables. Published 3D Lenia organisms already exist; any novelty claim needs a precise definition and literature comparison, for example with Chan's [higher-dimensional study](https://arxiv.org/abs/2005.03742).

For initial-condition statistics, begin with controlled **two-patch experiments**: sweep periodic separation relative to kernel radius, orientation, size, value distribution and shape. Record actual starting fields and repeated seeds. Classify outcomes at a stated observation horizon, retaining mixed, transient and unknown cases alongside descriptive labels such as spheres, tubes and extinction. Connected components must respect periodic boundaries. The measured outcome frequencies belong to that particular distribution and model, not to state space in general. Changes in grid extent and changes in spatial resolution require separate controls.

Prediction of 2D Orbium formation can then use the labeled runs. First compare simple interpretable features—mass, patch distances, overlap, spatial correlations and early-front measurements—against base-rate and short-simulation baselines. Distinguish a predictor using only the initial field from one observing part of the evolution. Split related trajectories and parameter families carefully when evaluating generalization, and include computation cost. A neural network is an option once the dataset and success criteria are reliable, rather than a prerequisite for the study.

## Legacy application

Build `Starter.sln` with Release|x64 to obtain `x64/Release/Leniascope.exe`. Its FFTW DLL and bitmap icons are copied beside it; run with that output directory as the working directory. The source and earlier UI remain for comparison. The sections below describe that original application and its historical development.

# Legacy features

Here's an annotated screenshot of the application:

<fig>
<img src=images/screenshot.jpg>
</fig>

The simplest way to play with the program is to press the **Randomize** button, which erases the buffer and re-initializes it randomly, wait to see if an interesting pattern emerges, then either
randomize again or alter the parameters.

The mathematical parameters are in the upper right corner:
- μ and σ are the mean and standard deviation of the normal distribution that determines the growth function
- ε is the step-size in the difference equation

The values are given as integer numerators of fractions, so "10³σ=17" means σ=0.017.

The **Save** button saves the current buffer, as well as the parameters, into a ".len" file, which **Load** can later recover. This way if you find a nice pattern you can record it and share with others.

The **Export** button saves the current buffer as a bitmap image. 

The **Pause/Play** is self-explanatory. It can be useful if you want to pick the right moment to export an image.

In the bottom right corner you can choose the color map that assigns a color to the real values. The choices are copied from Python's matplotlib, so they might be familiar.

There is also an experimental "3D Lenia" mode, which adds a third "z-axis" dimension to the grid. It displays one 2D "z-slice" at a time however, but you can change which slice that is by scrolling the middle mouse button.


# Historical implementation

Leniascope is coded in Visual Studio C++ using the Win32 API. This was partly an exercise to learn Windows programming, so the interface was coded entirely by hand rather than generated with Visual Studio. I learned quite a bit, including the fact that it's best to use the automated tools as much as possible to keep the repetitive parts of the code organized in a uniform way. On the other hand, some very basic features require altering the window message pipeline using tricks that are standard but not straight-forward or automatic.

The main computation  amounts to calculating a 2D convolution of the state grid with a fixed kernel, so the fast Fourier Transform comes in handy. Initially I used my own implementation of <a href=https://en.wikipedia.org/wiki/Cooley%E2%80%93Tukey_FFT_algorithm>Cooley-Tukey</a>'s radix-2 algorithm, including optimizations accounting for the input and output data being real numbers. You can find that implementation in `fft.cpp`, and the old code that uses it in the `customfft` branch. A Python version of the same implementation was comparable in speed to `numpy.fft.rfft2`. The current `master` branch however uses the <a href=https://www.fftw.org/>fftw</a> library, which has many fine-tuned optimizations, is several orders of magnitude faster, and is free. Using that the speed bottleneck is no longer the mathematical computation -- it's evenly divided between the step assigning RGB colors to real values at each pixel, and the step copying the color data into visual memory. Both of these are almost irreducible operations with not much room for improvement, but perhaps using multi-threading to do them in parallel can be one way to improve.

# General Thoughts

In Conway's Game of Life, random initial states usually lead to periodic patterns after a few turns. You get die-offs often, but you rarely see the entire grid fill out with a static repeating pattern. In Lenia on the other hand, most initial data of enough mass evolves into the same type of almost plant-like pseudo-periodic organism:

<img src=images\plants.jpg height=450 width=450>

One would think that the rules somehow do not force enough self-interactions, were it not for the existence of the "Orbium" (pictured before). It is really quite astonishing: it moves, has bilateral 
symmetry like actual living animals, and even looks vaguely like a jellyfish. It's also relatively stable under parameter change -- for instance in Leniascope you can first contruct an orbium with a small kernel radius of around 15, then slowly increase that radius as the program runs. The orbium will adapt and grow larger, and become more detailed. 

But in 3D Lenia, every random initial pattern that I've seen eventually ends up in either everything dying, or a 3D version of the same plant-like static pattern. It seems that having a third degree of spatial freedom is just not restrictive enough to force complex interactions. 

Consider the the following typical pattern in 2D Lenia:

<img src=images\plantorb.bmp height=450 width=450>

It is the same usual pattern in the process of spreading out to take over the grid. On the edges of its "wavefront" it is typically undulating and uneven, and often includes half-formed orbiums attempting to break away from the main mass. 

But in 3D Lenia you don't see this at all. The edges of the analogous spreading pattern are much more uniform, as if the mass is spreading without needing to compete with itself for space:

<img src=images\3dplants.bmp height=400 width=400>

Is it possible to find a more restrictive 3D generalization of Lenia that forces more space competition on spreading matter? Perhaps that is the way to find the 3D orbium.
