# 무선 실험판 한계

이 버전은 Switch 1 Pro Controller의 Bluetooth Classic 프로토콜을 Switch 2에서 시험하기 위한 코드입니다. Switch 2의 현재 펌웨어와의 호환성은 실기 연결 결과로 판단해야 합니다.

- 빌드/CRC/BT 스택 초기화 확인은 실제 Switch 연결 또는 플레이 검증을 뜻하지 않습니다.
- BOOTSEL 두 번 클릭으로 PC USB ↔ Switch Bluetooth 모드를 전환합니다. 선택은 저장됩니다. PC 모드에서는 본체 무선 연결과 깨우기가 비활성화되며, Switch 모드에서만 USB가 전원/진단용입니다.
- PC 입력·출력·오디오 경로는 복구했지만 게임별 햅틱/오디오 호환성은 실기 확인이 필요합니다. Pico W는 기존처럼 Opus 처리를 제외합니다.
- NFC/Amiibo와 Switch 무선 헤드셋/마이크/스피커 기능은 없습니다.
- Switch 2 슬립 깨우기는 [별도 설정이 필요한 실험 기능](SWITCH2_WAKE.md)입니다. 이미 등록된 조이콘 2/프로콘 2의 광고를 Pico가 직접 학습하며 PC Bluetooth는 필요 없습니다. 실제 본체 깨우기 성공은 미검증입니다.
- 자동 활 효과는 게임 상태를 감지하지 않습니다. 기본 Switch R2 저항입니다.
- 두 Bluetooth 링크의 장시간 안정성/무선 지연은 추가 검증이 필요합니다.
- Bluetooth 주소가 기존 USB 버전과 달라 DualSense 재페어링이 필요합니다.
- 이전 버전으로 되돌려도 키 상태에 따라 DualSense 재페어링이 필요할 수 있습니다.

현재 측정 결과는 dist/BUILD_REPORT.json 및 dist/WIRELESS_STATUS.json을 확인하세요.

연결은 되지만 SSP 교환 전에 50~90ms 만에 인증 실패(0x05)로 끊기는 경우, 본체 쪽 Bluetooth 상태도 원인일 수 있습니다. 다른 개발자의 동일 증상 기록은 https://github.com/cajunpanda/bluetooth-nes-advantage/blob/main/docs/switch_pro_protocol.md 를 참고합니다. 원인 확정 전에는 본체를 슬립이 아닌 완전 종료 후 다시 켜서 비교합니다. 다른 컨트롤러의 페어링 정보를 일괄 삭제하는 절차는 사용하지 않습니다.
