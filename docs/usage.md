# Using vv

[README](../README.md) · [Building](building.md) · [Contributing](../CONTRIBUTING.md)

## Inputs and command line

Run `vv <meshfile> [<meshfile2> ...]`. At least one file is required. Shell globs such as `vv /data/case/*.vtk` open each matching file.

| Input                     | Recognition and scope                                                                                                                       |
| ------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------- |
| Legacy VTK (`.vtk`)       | Recognized by its header; reads datasets including PolyData and unstructured grids, with all attribute blocks.                              |
| VTK XML PolyData (`.vtp`) | Recognized by `<VTKFile` in the header; the reader supports PolyData, not arbitrary VTK XML datasets such as `.vtu`.                        |
| VTKHDF (`.vtkhdf`)        | Requires the extension and an HDF5 signature; supports static meshes and temporal playback when the installed VTK reader supports the file. |
| XDMF (`.xdmf`, `.xmf`)    | Requires a lowercase extension; reads static topology/geometry and node/cell attributes backed by HDF5, including temporal collections.     |
| Mesh JSON                 | Recognized by content; mesh objects contain `vertices` and `indices`, directly, in an array, or under `surface`/`surfaces`.                 |
| EnSite DIF XML            | Recognized by `xml` and `DIF` in the header; reads volumes and vertex maps.                                                                 |
| CARTO mesh                | Recognized by `#TriangulatedMeshVersion2.0` in the header.                                                                                  |
| FreeSurfer surface        | Requires `.pial`, `.surf` or `.white`; reads binary triangular surfaces.                                                                    |
| LS-DYNA keyword mesh      | Recognized by `*KEYWORD` in the header; reads tetrahedral mesh parts and follows `*INCLUDE` files.                                          |
| STL (`.stl`)              | Requires the extension, case-insensitively; reads ASCII and binary surfaces.                                                                |
| NIfTI (`.nii`, `.nii.gz`) | Requires the extension, case-insensitively; reads single-component 3D volumes, placed by their sform or qform. An integer image with no scaling and at most 256 distinct nonzero values is a label map: each label becomes a named surface. Other volumes, such as CT, have no surfaces. Both open the [slice views](#volumes-and-slice-views). |

Use `-` once to read standard input, for example:

```sh
vv - < mesh.vtk
```

Stdin is copied to a temporary file before loading. Only content-recognized formats work this way: legacy VTK, VTK XML PolyData, mesh JSON, DIF XML, CARTO and LS-DYNA. Formats requiring a filename extension must be opened by path. For formats referencing other files, use a real input path so relative references resolve from its directory. Temporary files are removed after loading; failed reads or writes abort loading.

| Option                   | Effect                                                                                                                |
| ------------------------ | --------------------------------------------------------------------------------------------------------------------- |
| `-s`, `--split`          | Open every file in [split view](#single-and-split-view): a tile per scalar instead of one scalar at a time. `-e`, `--explode` is the same. |
| `-o`, `--overlay`        | Show all files together in one view instead of side by side; parts of multipart files are prefixed with their file name. Implied by NIfTI inputs. |
| `-C`, `--common-cat-lut` | Share the union of categories across categorical fields.                                                              |
| `-a`, `--annotate`       | Start in annotation mode; requires one file that is not a time series.                                                |
| `-r`, `--range min,max`  | Set the initial color range for every continuous field; bounds must be finite and `min < max`, e.g. `--range -80,40`. |
| `-v`, `--version`        | Print the version and build date, then exit.                                                                          |
| `-h`, `--help`           | Print the command-line reference, then exit.                                                                          |

Run `vv update` to install the latest release over a package from Releases. It needs `curl` and network access. The Linux AppImage and binary and the macOS `~/.local` tree are replaced in place; on Windows the new portable `.exe` is downloaded to your Downloads folder. Builds installed another way should be updated the same way. To open a mesh file named `update`, pass `./update`.

## Comparing files and fields

Each file opens in its own pane, laid out in an adaptive grid. All panes share a camera, preserving relative positions for meshes in a common coordinate frame. Colored letter badges identify files consistently in panes, Layers and probe cards.

A pane shows its own file's fields with its own pager and legend, so one file can show `potential` while the one beside it shows `dispersion`. A field of the same name and point/cell association in several files is one field: it shares its color range, colormap or palette and clip window wherever it shows.

Keys, the Color panel and the command palette act on the active pane: the one you last clicked, paged or opened a menu on. Among several panes its pager carries an accent outline.

### Single and split view

A pane starts in single view: one field at a time, stepped with its pager, plus plain geometry. Split view shows a tile per field instead. Switch a pane with the grid button at the end of its pager, by right-clicking its mesh and choosing **Split view**, or with **T** for the active pane. Each pane keeps its own view, so a split pane can sit beside a single one. `--split` opens every file split.

When a split pane's fields cannot all fit at a usable tile size, its pager shows a page of them, up to nine, and steps through the pages; clicking its name opens the full field list and jumps to the selected field's page. Paging preserves the camera, clip ranges and colors. Switching back to single view shows the first field of the page.

Legends shrink with their tiles. Categorical legends use chips when rows would take too much space; hover a `+N` chip to list hidden categories, or click it to edit one. Small legends expose the field name in a tooltip.

### Linked fields

**Link fields across files**, in a pane's right-click menu and the command palette, makes panes follow each other: showing a field in one pane shows it in every pane whose file has it. Files that carry the same fields start linked, so stepping compares one field across them; files with different fields start independent.

## Fields, ranges, and colors

A pane's pager shows its field, the field's point/cell association and its position in the file's list. Its arrows step fields, including plain geometry; clicking the name, or **Choose field…** in the pane's right-click menu, opens a searchable list. Single-component arrays are scalar fields; three-component arrays are available as [vector glyphs](#view-controls-and-probing).

Point-data fields interpolate across triangles; cell-data fields stay flat per cell. Fields with 2 to 20 distinct finite values, or one finite value plus NaN, are categorical and use one color per category. Other scalar fields use a continuous colormap.

### Continuous fields

Open **Color** to choose the colormap of the active pane's field, reverse it or adjust its range. Choices are grouped as sequential (rainbow, viridis, magma, inferno, plasma, cividis, turbo), diverging (coolwarm, RdBu) and cyclic (hue wheel, twilight). Rainbow is the default; names containing `phase` start on the hue wheel. Each field keeps its choice until vv exits.

Drag a handle on the legend or the Color panel's histogram to change the clip window. Double-click a legend bound, or edit Min/Max in Color, for a numeric value. **Use data range** resets the window. **Outside range** selects endpoint colors (Clamp) or gray for values beyond it. These settings are retained per field, also while another field shows.

Legend bounds round to the range's scale, retaining at least two significant digits for nonzero values. Hovering and editing reveal precise values; typed bounds keep their meaningful digits until dragged or reset.

The initial range comes from a static field's values or, for a time series, up to 16 evenly spaced frames. The sampled temporal range stays fixed during playback but can miss extrema between samples. `--range min,max` overrides that initial range for every continuous field; cyclic data often needs an explicit cycle, e.g. `--range 0,260`.

Every legend edits its field in place: click a continuous legend's gradient or right-click its card to choose or reverse a colormap. Coloring and clip edits apply wherever the field shows, in other panes and on other pages.

### Categorical fields

**Default** uses file-provided category colors, or ten colors chosen for the dark view. Legacy VTK colors come from a field's `LOOKUP_TABLE`, which VTK attaches only to the first `SCALARS` block of each point/cell association. Categories index the table directly when all values are whole numbers within it; otherwise entries are assigned in category order. Mesh JSON can provide painter colors.

The Color panel, legend's Palette menu and command palette also offer Tab10, Tab20, Okabe-Ito (colorblind-safe), Set2, Paired and Dark2. A whole-number category takes the palette entry at its value, so it keeps its color in a file missing another category; other categories take entries in order. The entries continue past the palette with lighter, then darker variants.

Click a legend swatch or right-click its row to recolor a category or reset it to its palette color. **More colors…** opens a full picker. Editing a finite category creates **Custom**, which overlays your choices on the current palette. Switching palettes retains Custom; editing another category starts a new Custom over the palette then shown. Choices last until vv exits and apply wherever the field shows.

Categorical fields containing NaNs also show a **NaN / No value** swatch in the legend and Color panel. Edit or reset it with the same color menu. Its color is independent of the finite categories and stays unchanged when switching palettes.

With `--common-cat-lut`, categorical fields share their union of values. A value takes the same color wherever the same palette is selected. Default then uses vv's ten colors rather than a field-specific file table.

## View controls and probing

Hover a tool-rail button for its name and shortcut. Unavailable tools are dimmed and explain why in their tooltip.

| Tool          | Action                                                                                      |
| ------------- | ------------------------------------------------------------------------------------------- |
| Layers        | Show or hide files and their parts.                                                         |
| Color         | Edit the active field's [colors and range](#fields-ranges-and-colors).                      |
| Style         | Select Solid, Solid with edges, Wireframe, X-ray or Glass.                                  |
| Probe         | Read field values under the pointer or pin a location.                                      |
| Annotate      | [Paint integer cell labels](#annotation).                                                   |
| Vector glyphs | Cycle three-component point/cell arrays, then off; each vector is a short oriented segment. |
| Reset view    | Restore the opening camera.                                                                 |
| Save snapshot | Save a PNG of the view, mesh identities and legends, excluding controls.                    |

Only one of Layers, Color and Style opens at a time. Clicking outside the panel and rail closes the panel while preserving the click or drag's normal action. Color edits the field of the active pane in single view; in split view, use the tiles' legends. Vector glyphs require a three-component array.

### Layers and styles

Layers groups each file's parts beneath a chevron. Click an eye to toggle visibility; Alt-click (Option-click on macOS) to isolate that file or part, then repeat to show everything. The context menu also offers **Show only this** and **Show all**.

Style applies to every panel and persists across fields and playback. X-ray blends translucent surfaces in their field or part colors, with an Opacity slider. Glass shows a smooth shell with a bright rim, glossy highlights and open-border outlines; Density controls opacity away from the rim. Each style retains its slider value. Toggling X-ray or Glass again restores the previous opaque style.

### Probe

Enable Probe and hover a mesh to read the active value and cell ID, plus the file/part identity when needed. Point fields interpolate within the cell, categories include their legend swatch, and NaN reads `no value`.

Matching locations produce readings in every pane and tile on view. Matching uses world coordinates within numerical tolerance; each tile reads its own field and cell, not an assumed common cell ID.

Click a mesh to pin its location. A marker and card follow camera, frame and field changes. Clear the pin with the card's close button, an empty-space click or by disabling Probe. Escape closes an open panel before releasing the pin. Hidden meshes hide their probe presentation and restore it when shown. Probe remains enabled while a tool panel is open.

### Command palette

The command palette searches fields, the active field's colormaps or categorical palettes, and viewer commands. Select a result with the arrow keys and Return, or click it. Unavailable commands remain visible with an explanatory tooltip. See [keyboard shortcuts](#keyboard-shortcuts) for opening and dismissing search.

## Annotation

Open Annotate to paint a per-cell integer `label` array, or start with `vv -a mesh.vtk`. An existing integer `label` array is edited in place; otherwise one is created. When the shown field is categorical with at most 9 categories, Annotate fixes that field in place instead: swatches 1 to N are its categories in legend order, with their colors and with whole-number gaps filled in when they fit and then the next value when a swatch is left, so a category the file lacks can be painted back, and erasing leaves no value (NaN in a floating-point array, otherwise 0). A point field takes the points of the brushed cells. The mesh displays labels with 0 (unlabeled) in gray.

- **Rotate**, **Paint** and **Erase** choose the left-drag action. Hold-to-paint/erase keys temporarily override it; right-drag erases while Paint or Erase is selected.
- Swatches show the values they write, or their keys 1 to 9 when a value is too long. Keys 1 to 9 select a swatch by position. The brush paints the hovered cell and the chosen number of neighboring rings along the surface, without jumping to nearby disconnected sheets. A preview shows the affected cells.
- **Undo** reverts up to 50 strokes.
- **Save** (Ctrl+S) replaces the opened file with the labeled mesh, after a confirmation whose **Don't ask again** box turns it off. It applies to a single-mesh `.vtk`, `.vtp` or `.vtu` file; otherwise it asks for a file like Save as.
- **Save as…** (Ctrl+Shift+S) writes the labeled mesh to `.vtp` for PolyData or `.vtu` for an unstructured grid. VTK XML `.vtu` output is not currently supported as input by vv.
- Closing the annotation bar restores the scalar field and retains labels for another editing session in the same window. Closing vv with unsaved labels asks whether to save (as Save does), discard or cancel.

Annotation requires a single file and is unavailable for a time series, whose frames replace mesh arrays. Entering it puts the pane in single view. For a multipart file, only its first part receives labels. During annotation, digits select labels, Color is unavailable and field cycling keeps labels visible. Probe reads labels while Rotate is selected. The bottom strip shows annotation keys.

## Volumes and slice views

Opening a NIfTI file adds axial, coronal and sagittal slice views beside the 3-D view, which then shows every file together, as with `--overlay`, plus the three grayscale planes.

The first intensity volume is shown in grayscale; further intensity volumes are ignored. Label maps are drawn over it in their surfaces' colors, resampled onto its voxel grid when theirs differs. Without an intensity volume, the first label map's grid is used on black. Views follow the radiological convention for RAS world coordinates, with patient left on screen right, so a file whose header mislabels its orientation shows flipped.

| Input in a slice view              | Action                                                  |
| ---------------------------------- | ------------------------------------------------------- |
| Left click or drag                 | Move the crosshair; the other views follow.             |
| Wheel, Up / Down                   | Previous or next slice; Page Up / Page Down step by 10. |
| Ctrl+wheel; Command+wheel on macOS | Zoom.                                                   |
| Middle drag                        | Pan.                                                    |
| Right drag                         | Window (horizontal) and level (vertical).               |

**M** switches labels between outlines and translucent filled masks. Hiding a label in Layers also hides it in the slice views, and **R** resets their zoom and pan with the 3-D camera.

## Time series

Temporal VTKHDF and XDMF inputs add a playback bar under the panes with frame stepping, play/pause, a seekable timeline, speed from 1× to 12× and Loop. Without Loop, playback stops at the final frame. A time series opened alone has its pager in the bar; beside other files, every pane keeps its own pager, so the series can play in one pane while a static file shows another field next to it. Space changes meaning when the playback bar is present, as shown below.

Frames read only the fields the series' pane shows: one in single view, the page's in split view. Only the first temporal file plays.

Pin a probe on the time series to trace the field of the pinned tile at that location across frames. The trace fills incrementally, coarse to fine, while the camera and playback remain responsive. It uses the legend's range and restarts when the field or point changes. Color ranges remain stable using the [sampling rules](#continuous-fields).

## Keyboard shortcuts

Letter keys below do not require Shift. Shortcuts apply to the viewer; focused editors and search lists handle their own input.

| Key                               | Action and context                                                                                                |
| --------------------------------- | ----------------------------------------------------------------------------------------------------------------- |
| Space                             | Next field of the active pane, or next page in split view; play/pause when the playback bar is present.           |
| Shift+Space                       | Next field or page of the active pane, including during playback.                                                 |
| Ctrl+Space; Option+Space on macOS | Previous field or page of the active pane.                                                                        |
| T                                 | Switch the active pane between single and split view.                                                             |
| Right click on a mesh             | That pane's menu: Split view, Choose field… and, among several files, Link fields across files.                   |
| Left / Right                      | Previous / next frame in a time series.                                                                           |
| P / B                             | Toggle Probe / Annotate.                                                                                          |
| V                                 | Next vector array, then off.                                                                                      |
| R                                 | Reset the camera.                                                                                                 |
| X / G                             | Toggle X-ray / Glass.                                                                                             |
| S / W                             | Solid / Wireframe.                                                                                                |
| 1 to 9                            | Select a label during annotation.                                                                                 |
| Hold A or J / E or K              | Paint / erase during annotation.                                                                                  |
| + or = / - or _                   | Increase / decrease brush rings during annotation.                                                                |
| Ctrl+Z; Command+Z on macOS        | Undo an annotation stroke.                                                                                        |
| Ctrl+S / Ctrl+Shift+S             | Save annotations in place / to a new file.                                                                        |
| Ctrl+K; Command+K on macOS        | Toggle the command palette.                                                                                       |
| Up / Down, Return                 | Navigate and select in a field list or command palette.                                                           |
| Escape                            | Dismiss search; otherwise close the open panel, release the probe pin, then leave annotation, one step per press. |
| M                                 | Label outlines or filled masks in the slice views.                                                                |
| Q                                 | Close vv, with an unsaved-label prompt when needed.                                                               |
