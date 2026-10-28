# 무선 출력 실험판 구조

DualSense와 Switch 호스트가 한 CYW43439에서 서로 다른 Classic ACL 연결을 사용합니다.
bt.cpp는 DualSense의 ACL/Control/Interrupt를 관리합니다. switch_bt.cpp는 별도의 콘솔 주소/ACL/Control/Interrupt, SDP 서버 레코드, 재연결 및 HID 전송 대기를 관리합니다.
공유 L2CAP 서비스의 콜백은 먼저 Switch 소유의 주소/채널인지 검사하고, 나머지는 기존 DualSense 처리기로 전달합니다.
센서는 기존 DualSense 파서 → MotionCalibration → SwitchProProtocol의 MotionResampler → A1/30 Bluetooth 입력 보고서로 전달합니다.
Switch의 A2/01 및 A2/10 출력은 프로토콜 처리기 → 진동 어댑터/PCM 합성 → DualSense Bluetooth 출력으로 돌아갑니다.
EffectsRuntime과 진동 어댑터는 USB 상태 대신 두 무선 연결과 입력 신선도를 사용합니다. PC USB의 게임 출력 보고서는 무시합니다.
USB 디스크립터는 진단 호환성을 위해 Sony 식별을 유지하며 F8 버전과 F9의 PW/WL 서명으로 실험판을 식별합니다.
BOOTSEL 두 번은 Switch 재페어링 대기, 한 번은 DualSense 연결/검색, 누른 채 USB 연결은 ROM 업데이트 모드입니다.
