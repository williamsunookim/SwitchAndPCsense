# 검증 상태

build.ps1 -Test는 ARM 펌웨어 빌드, 메모리 예산 확인, C++ 호스트 테스트 10개, Python 테스트 14개를 실행합니다.
새 테스트는 Bluetooth 입력 보고서/초기화/Switch 유선 비트, USB 절전 상태에서의 자동 트리거/진동, 두 무선 연결 중 하나가 끊겼을 때의 정지 경로를 검증합니다.
호스트 테스트는 실제 HCI 무선 장치나 Switch 콘솔을 구동하지 않습니다.
tools/diagnostics/verify_board.py는 실행 버전, UF2와 플래시 CRC, CPU 및 시작 시 합성/보간 벤치마크를 확인합니다.
tools/diagnostics/wireless_status.py는 DualSense 연결과 Switch 연결, 전송/수신 보고서 수를 USB HID에서 읽습니다.
실기 플레이, 무선 왕복 지연, 장시간 안정성은 별도 실기 검증 항목입니다.
