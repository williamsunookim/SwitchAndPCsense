# 참고 소스와 검토 범위

SDP/HID 정의와 Nintendo 식별/페어링 흐름은 SWITCH_BT_NOTICE.md의 MIT 소스를 참고하거나 고지를 보존해 적용했습니다.
DualSense 입력, 공장 IMU 보정, 기존 Switch 센서/진동 변환은 0.3.1에서 유지했습니다.
새 전송 코드의 ACL 핸들, L2CAP 채널, 연결 해제, 명령 응답, USB 출력 격리와 효과 정지 경로를 검토했습니다.
실제 두 기기의 동시 무선 연결은 코드 검토만으로 검증할 수 없으므로 실기 상태 보고서와 플레이 확인이 필요합니다.

SDK의 btstack_hci_transport_cyw43.c는 전송 open 단계에서 BD_ADDR 설정을 덮어씁니다. READ_LOCAL_VERSION_INFORMATION 완료 이벤트에서 실험판의 주소를 다시 적용하며, 실제 gap_local_bd_addr 결과를 진단 보고서로 확인했습니다. 이 처리는 공유 SDK 소스를 변경하지 않습니다.
