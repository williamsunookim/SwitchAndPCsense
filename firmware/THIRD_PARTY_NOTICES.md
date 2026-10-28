# Third-party notices

The Switch Pro USB descriptors and protocol behavior in `src/switch_pro_*` are
based on the GP2040-CE Switch Pro driver:

- Copyright (c) 2021 Jason Skuby (mytechtoybox.com)
- Copyright (c) 2024 OpenStickCommunity (gp2040-ce.info)

GP2040-CE is licensed under the MIT License:

> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to deal
> in the Software without restriction, including without limitation the rights
> to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in all
> copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
> SOFTWARE.


## Integrated development build

Based on thsrhwk01/Pico-DualSense-Switch-Bridge (6526c9de38437d172888acb7b5fe97701f20c6fb),
originally derived from awalol/DS5Dongle. Existing copyright/license headers are retained.
New trigger_engine/effects_runtime modules use MIT. See ../docs/SOURCE_REVIEW.md for reuse scope.

DS5Dongle-Studio (artzox/awalol, MIT) and PicoSwitch2 (Apache-2.0) were source-code
references. Their libraries/application code are not added as linked firmware dependencies.

IMPORTANT: src/audio.cpp carries MPL-2.0 at file scope, including our session-reset
modifications. The modified source and MPL license are provided with this firmware.
The repository-level MIT license does not replace this file-specific license.

Runtime dependencies: Pico SDK BSD-3-Clause; TinyUSB MIT; Opus COPYING (BSD-style);
Cockos WDL resampler permissive notice (the alternative LGPL option is not selected).
BTstack and CYW43 include both their original licenses and Raspberry Pi supplemental
licenses. The supplemental permissions apply to the named Raspberry Pi hardware;
no unrestricted license for unrelated hardware is asserted. Mbed TLS, CMSIS and
pico_printf notices are also included. Full texts are in licenses/.

Source changes: src/audio.cpp now resets session buffers/queues. Other modifications
add trigger profiles, output arbitration, safer input/connection handling, haptic
transitions and separate settings flash storage. Original WDL/Opus sources remain unchanged.


0.2.0 adds original MIT motion_resampler/haptic_calibration modules and changes Switch DSP.
The frequency curve is an experimental attenuation preset, not measured controller calibration.

0.3 Pico W does not link Opus. Its source and license are retained for provenance.
Modified MPL-2.0 audio.cpp now omits speaker/mic processing in this target.
Q16 motion optimization and resource_benchmark are MIT additions.
