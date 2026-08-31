/* nvk_GUI (ReaImGui fork)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef NVKGUI_OBJC_PREFIX_H
#define NVKGUI_OBJC_PREFIX_H

// Objective-C classes register process-wide by name. Prefix ours at the
// token level so the fork can coexist with a stock ReaImGui loaded in the
// same REAPER instance (identical class names would silently make one
// plugin's implementation serve both).
#define EventHandler        NvkGuiEventHandler
#define InputView           NvkGuiInputView
#define OpenGLLayer         NvkGuiOpenGLLayer
#define SWELLWindowOverride NvkGuiSWELLWindowOverride

#endif
