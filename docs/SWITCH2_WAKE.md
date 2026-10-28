# Switch 2 슬립 깨우기 (실험 기능)

듀얼센스가 Pico에 재연결되거나, Switch 연결이 끊긴 상태에서 PS 버튼을 누르면
Nintendo 형식의 BLE 깨우기 광고를 2초 동안 송신합니다. 광고 종료 후 5초간은
재시도를 제한합니다. Switch 무선 연결이 돌아오면 광고를 중지합니다.
USB의 PC 깨우기 설정(`enable_wake`)과는 별개의 기능입니다.

`0.4.3`부터 BOOTSEL 두 번 클릭으로 **PC USB ↔ Switch Bluetooth**를 전환합니다.
깨우기는 **Switch Bluetooth 모드에서만** 동작합니다. PC에서 현재 모드를
확인하거나 전환하려면 다음 명령을 사용합니다. 선택은 전원을 껐다 켜도 유지됩니다.

`0.4.4`부터 보드 LED로도 선택한 모드를 구분할 수 있습니다.

- **한 번 짧게 깜빡 → 긴 쉼 → 반복:** PC USB 모드
- **두 번 짧게 깜빡 → 긴 쉼 → 반복:** Switch Bluetooth 모드

반복 주기는 약 2초입니다. 페어링 중의 빠른 연속 점멸, 배터리 부족 경고,
페어링 초기화 확인 표시는 모드 표시보다 우선합니다. LED는 모드 선택을 나타내며
본체 연결 성공 여부는 별도로 진단해야 합니다. LED 끄기 설정이 켜져 있으면
모드 표시는 꺼집니다.

```powershell
python tools/config/output_mode.py status
python tools/config/output_mode.py switch
python tools/config/output_mode.py pc
```

**이 저장소에서는 실제 Switch 2 슬립 해제를 아직 검증하지 않았습니다.**
패킷/상태 전이/저장 테스트와 펌웨어 빌드는 실기 성공을 뜻하지 않습니다.
완전 종료 상태의 전원 켜기가 아니라 슬립 해제를 위한 기능입니다.

## 최초 설정

Switch 1 프로콘으로 페어링하는 것만으로는 Switch 2의 BLE 깨우기 등록이
완료되지 않습니다. 현재 구현은 **이미 자기 Switch 2에 페어링된 조이콘 2 또는
프로콘 2의 깨우기 광고를 재전송**합니다. 해당 컨트롤러가 한 번 필요합니다.
Switch 2 컨트롤러를 새로 페어링하는 USB/GATT 구현은 포함하지 않습니다.

1. 보드에 맞는 새 `dist/Pico-W.uf2` 또는 `dist/Pico-2-W.uf2`를 올립니다.
2. PC에서 도구 의존성을 설치합니다. 캡처에는 Bluetooth LE 어댑터가 필요합니다.

   ```powershell
   python -m pip install hidapi bleak
   python tools/config/switch2_wake.py capture
   ```

3. 20초의 캡처 시간 안에 Switch 2를 슬립시키고, **원래 컨트롤러의 HOME**으로
   깨웁니다. 가까운 PC에서 광고를 받으면 `dist/switch2-wake.json`이 생성됩니다.
   여러 컨트롤러가 감지되면 `capture --source AA:BB:CC:DD:EE:FF`로 선택합니다.
4. 원래 컨트롤러를 끕니다. Pico를 PC USB에 연결하고 설정을 저장합니다.

   ```powershell
   python tools/config/switch2_wake.py configure dist/switch2-wake.json
   ```

5. Pico의 전원을 뺐다 다시 연결합니다. **Pico의 Bluetooth 공개 주소가 캡처한
   컨트롤러 주소로 바뀝니다.** 기존 주소에 속한 링크 키는 무효화되므로,
   듀얼센스와 Switch 무선 연결을 다시 페어링합니다. 본체의 다른 컨트롤러
   등록 정보를 일괄 삭제할 필요는 없습니다.
6. Switch Bluetooth 모드를 선택하고 Pico를 계속 켜 두세요. 본체 페어링이 필요하면
   `python tools/config/output_mode.py pair`를 실행하고 본체의 잡는 방법/순서 변경을 엽니다.
   Switch 2를 슬립시킨 뒤 듀얼센스 PS를 누릅니다.
   재연결 시 첫 PS 입력을 못 받아도 듀얼센스 연결 완료 시 한 번 요청합니다.

Pico와 원래 컨트롤러가 같은 무선 주소를 사용하므로 이 설정을 사용하는 동안에는
**원래 컨트롤러를 함께 켜지 마세요.** 본체에서 해당 컨트롤러의 등록을 삭제하면
깨우기가 동작하지 않을 수 있습니다. 본체 업데이트에 따른 동작도 실기 확인이 필요합니다.
Pico의 전원은 슬립 중에도 유지되어야 합니다.

## 진단 및 되돌리기

```powershell
python tools/config/switch2_wake.py status
python tools/config/switch2_wake.py wake
python tools/config/switch2_wake.py disable
```

`wake`는 PC에서 송신을 요청하는 진단 명령입니다. 본체가 이미 무선 연결된
상태에서는 요청이 취소됩니다. `attempts`는 송신 요청 횟수이며 본체가 깨어난
횟수가 아닙니다. `hci_error`는 BLE 설정/송신 명령 오류 코드이고,
`advertising_requested`는 호스트가 광고를 요청한 상태입니다. 성공 확인 응답은 없습니다.

`disable` 후 전원을 다시 연결하면 보드 고유 주소로 돌아갑니다. 이때도
듀얼센스와 Switch 연결을 다시 페어링해야 합니다. 설정 저장은 기존 설정용
플래시 페이지의 별도 CRC 레코드를 사용하며 기존 트리거 설정을 보존합니다.

직접 작성한 JSON도 사용할 수 있습니다. `source`는 본체에 이미 BLE 등록된
컨트롤러의 Bluetooth 주소, `target`은 해당 깨우기 광고에 포함된 본체의
Bluetooth 주소입니다. Wi-Fi MAC이나 Switch 1 프로콘의 주소로 대체하지 마세요.
`product`는 조이콘 2 L=8294 (`0x2066`), R=8295 (`0x2067`), 프로콘 2=8297 (`0x2069`)입니다.

## 프로토콜 근거

- [ndeadly: Switch 2 Bluetooth interface](https://github.com/ndeadly/switch2_controller_research/blob/master/bluetooth_interface.md): Nintendo 제조사 데이터와 본체 주소의 바이트 순서.
- [alexvnesta: wake packet research](https://github.com/alexvnesta/switch2controller/blob/master/docs/research/wake-packet.md): 등록된 송신 주소가 필요하며 비연결 광고로도 깨우기를 재현한 기록.
- [tv: Switch 2 wake-up](https://github.com/tv/switch2-wake-up): 실제 조이콘 캡처를 이용한 재전송 사례.

위 자료의 프로토콜 설명을 참고해 구현했으며, 외부 프로젝트의 소스 코드를
복사하거나 의존성으로 추가하지 않았습니다.
