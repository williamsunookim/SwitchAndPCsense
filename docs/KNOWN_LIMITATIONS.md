# 무선 실험판 한계

이 버전은 Switch 1 Pro Controller의 Bluetooth Classic 프로토콜을 Switch 2에서 시험하기 위한 코드입니다. Switch 2의 현재 펌웨어와의 호환성은 실기 연결 결과로 판단해야 합니다.

- 빌드/CRC/BT 스택 초기화 확인은 실제 Switch 연결 또는 플레이 검증을 뜻하지 않습니다.
- PC USB는 전원/진단만 제공하며 PC 게임 입력·오디오를 제공하는 통합판이 아닙니다.
- NFC/Amiibo, 헤드셋/마이크/스피커, Switch 본체 깨우기 기능은 없습니다.
- 자동 활 효과는 게임 상태를 감지하지 않습니다. 기본 Switch R2 저항입니다.
- 두 Bluetooth 링크의 장시간 안정성/무선 지연은 추가 검증이 필요합니다.
- Bluetooth 주소가 기존 USB 버전과 달라 DualSense 재페어링이 필요합니다.
- 이전 버전으로 되돌려도 키 상태에 따라 DualSense 재페어링이 필요할 수 있습니다.

현재 측정 결과는 dist/BUILD_REPORT.json 및 dist/WIRELESS_STATUS.json을 확인하세요.

연결은 되지만 SSP 교환 전에 50~90ms 만에 인증 실패(0x05)로 끊기는 경우, 본체 쪽 Bluetooth 상태도 원인일 수 있습니다. 다른 개발자의 동일 증상 기록은 https://github.com/cajunpanda/bluetooth-nes-advantage/blob/main/docs/switch_pro_protocol.md 를 참고합니다. 원인 확정 전에는 본체를 슬립이 아닌 완전 종료 후 다시 켜서 비교합니다. 다른 컨트롤러의 페어링 정보를 일괄 삭제하는 절차는 사용하지 않습니다.
