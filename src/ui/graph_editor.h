#pragma once
// Dockable graph editor (Maya/Blender style) for the clip's key layer.
//
// Unlike the viewport dope-sheet, which edits one channel in its natural units, the graph editor
// draws many channels at once with independent value normalisation:
//   Normalised  - every channel is stretched to 0..1 of its own range (weights 0..1, bone axes
//                 ±range°) so a 0.3 mouth weight and a 12° head turn share the same vertical space.
//   Absolute    - one value axis (channel units); useful when comparing channels of the same kind.
//   Stacked     - each visible channel in its own strip.
// Keys of every visible channel are editable together: click / box-select across channels,
// drag in time (frame-snapped unless Ctrl) and value, drag tangent handles, Delete, tangent
// modes, K to key the visible channels at the playhead, F to frame the selection.
#include <string>
#include <vector>

namespace fr {
class Application;

namespace graph {
/// Draws the window when open (call every frame after the main panels). Returns true when it consumed keyboard input.
bool draw(Application& app);
void setOpen(bool open);
bool isOpen();
/// Makes a channel visible and current (e.g. from the timeline's channel combo). `channel` indexes tl::channels(clip).
void showChannel(int channel, bool solo = false);
/// Selected keys as (channel, key) pairs, for tests / scripting.
std::vector<std::pair<int, int>> selection();
} // namespace graph
} // namespace fr
