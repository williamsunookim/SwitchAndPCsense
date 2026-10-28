# integrated-0.4.1-gyro-fix

- Diagnostics page 4: DualSense sensor-clock spacing histogram, to tell lost Bluetooth reports from bursty delivery.
- Gyro integrated on the DualSense sensor clock (report bytes 27..30, 1/3 us) (trapezoid rule tried and reverted; each rate covers the interval since the previous report). Bursty Bluetooth arrivals lost rotation with arrival-time spacing; captured data showed ~10% (slow) to much larger (fast) pitch under-rotation, which the game's gravity correction then dragged back.
- Angle-conserving gyro: DualSense rotation is integrated on arrival and the not-yet-sent rotation is spread over each report's three 5 ms slots. Radio gaps neither add phantom rotation nor drop rotation (dropped rotation made the game's tilt correction drag the view after a stop).
- Radio gaps no longer repeat the last gyro rate (caused the view to keep turning after the controller stopped). Gyro held at most 12 ms past the newest sample, then zero; accel still held.
- Fix Switch wireless drop ~10 s after connect: an abandoned outgoing L2CAP request (lost race with the console's own channels) timed out (RTX 0x69) and tore down the working link. Now ignored unless it is a live channel. Confirmed from on-device event history.
- Wireless event history 3 -> 16 entries with 32-bit time; trace codes for every local disconnect path; tools/diagnostics/read_link_diag.py.
- Watchdog stage tracking across resets (reboot diagnostics).
- Report Pro Controller firmware 3.72 (was 4.145): Switch 2 then enables IMU mode 0x01 instead of undocumented mode 0x02, which misread raw samples as violent rotation. Confirmed on hardware via diagnostics.
- Radio gaps hold the newest motion sample instead of sending zero (free-fall).
- Motion playout delay removed (10 ms -> 0 ms).
- Motion diagnostics page (F6 0x30 + F9) and tools/diagnostics/read_motion_diag.py.
- Host test runner now catches negative (crash/assert) exit codes; previously such failures were reported as passed.
- Subtract DualSense factory gyro bias before sensitivity gain (it was read but discarded, causing Switch motion drift).
- Automatic rest re-zero: within 2 deg/s, steady 1.5 s near 1 g. Mute+Create manual zero kept.
- Tests for bias, auto re-zero, and slow-pan/oscillation non-absorption.

# 0.4.0 Pico W 무선 실험판

0.3.1 소스에서 분리했습니다. 기존 버전은 수정하지 않았습니다.
Switch Bluetooth 출력과 SDP 식별, 두 ACL 연결 및 채널 소유권 구분, 호스트 주소 보관/재접속을 추가했습니다.
USB와 독립적인 Switch 진동/자동 활 효과, PC 출력 격리, 무선 진단 보고서를 추가했습니다.
현재 검증 범위는 dist 보고서를 확인하세요.


