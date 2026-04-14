# STM32 JSON Communication Library

XBee DigiMesh를 통한 JSON 기반 통신 라이브러리입니다.
Fragment Protocol을 사용하여 대용량 JSON 메시지(최대 2KB)를 안정적으로 전송합니다.

## 지원 MCU

| 시리즈 | 정의 매크로 | 테스트 상태 |
|--------|-------------|-------------|
| STM32F3 | `STM32F3` | Tested |
| STM32F4 | `STM32F4` | Supported |
| STM32F7 | `STM32F7` | Supported |
| STM32H7 | `STM32H7` | Supported |
| STM32G4 | `STM32G4` | Supported |
| STM32L4 | `STM32L4` | Supported |

## 폴더 구조

```
stm32_json_com/
├── Inc/                    # 헤더 파일
│   ├── json_com.h          # JSON 통신 메인 API
│   ├── uart_queue.h        # UART 큐 드라이버
│   ├── xbee_api.h          # XBee API Mode 2 파서
│   ├── fragment_protocol.h # Fragment Protocol 정의
│   ├── fragment_rx.h       # Fragment 수신기
│   ├── fragment_tx.h       # Fragment 송신기
│   ├── device_hal.h        # App_* 함수 인터페이스
│   ├── cJSON.h             # JSON 파서
│   └── crc16.h             # CRC-16 계산
└── Src/                    # 소스 파일
    ├── json_com.c
    ├── uart_queue.c
    ├── xbee_api.c
    ├── fragment_rx.c
    ├── fragment_tx.c
    ├── cJSON.c
    └── crc16.c
```

## 빌드 설정

### STM32CubeIDE

1. **Include Paths 추가**
   - Project → Properties → C/C++ Build → Settings
   - MCU GCC Compiler → Include paths
   - 추가: `../Lib/stm32_json_com/Inc`

2. **Source Folders 추가**
   - Project → Properties → C/C++ General → Paths and Symbols
   - Source Location → Add Folder
   - 추가: `Lib/stm32_json_com/Src`

3. **MCU 시리즈 정의 확인**
   - Project → Properties → C/C++ Build → Settings
   - MCU GCC Compiler → Preprocessor
   - `STM32F3` 또는 해당 시리즈 매크로가 정의되어 있는지 확인

### Makefile

```makefile
# Include paths
C_INCLUDES += -ILib/stm32_json_com/Inc

# Source files
C_SOURCES += \
Lib/stm32_json_com/Src/json_com.c \
Lib/stm32_json_com/Src/uart_queue.c \
Lib/stm32_json_com/Src/xbee_api.c \
Lib/stm32_json_com/Src/fragment_rx.c \
Lib/stm32_json_com/Src/fragment_tx.c \
Lib/stm32_json_com/Src/cJSON.c \
Lib/stm32_json_com/Src/crc16.c

# MCU 시리즈 정의 (예: STM32F7)
C_DEFS += -DSTM32F7
```

## 사용법

### 1. 초기화

```c
#include "uart_queue.h"
#include "json_com.h"

UART_Context uart_ctx;
JSON_Context json_ctx;

int main(void) {
    // HAL 초기화 후...
    
    // UART 큐 초기화
    UART_Queue_Init(&uart_ctx, &huart1);
    
    // JSON 통신 초기화 (Device ID = 1)
    JSON_COM_Init(&json_ctx, &uart_ctx, 1);
    
    while (1) {
        // 메인 루프에서 주기적 호출
        JSON_COM_Process(&json_ctx);
        
        // 100ms마다 타임아웃 처리
        if (HAL_GetTick() - last_tick >= 100) {
            JSON_COM_Tick(&json_ctx);
            last_tick = HAL_GetTick();
        }
    }
}
```

### 2. App_* 함수 구현

`device_hal.h`에 정의된 App_* 함수들을 별도 파일에서 구현합니다:

```c
// user_app.c
#include "device_hal.h"

bool App_Ping(void) {
    return true;
}

bool App_Move(uint8_t motor_id, int32_t raw_pos) {
    // 실제 모터 제어 코드
    Motor_MoveToRawPosition(motor_id, raw_pos);
    return true;
}

int App_GetFiles(AppFileInfo *out_files, uint16_t max_count) {
    // SD 카드에서 파일 목록 읽기
    return SD_ListFiles(out_files, max_count);
}
```

## Git Submodule로 사용

```bash
# 프로젝트에 submodule 추가
git submodule add <repository_url> Lib/stm32_json_com

# 클론 시 submodule 포함
git clone --recursive <project_url>

# 기존 클론에서 submodule 초기화
git submodule update --init --recursive
```

## 프로토콜 상세

### JSON 메시지 형식

```json
// Request
{"msg":"req", "src_id":1, "tar_id":2, "cmd":"ping"}

// Response
{"msg":"resp", "src_id":2, "tar_id":1, "cmd":"ping", "status":"ok"}

// Event
{"msg":"evt", "src_id":1, "cmd":"motor_state", "payload":{...}}
```

### 지원 명령어

| 명령어 | 설명 |
|--------|------|
| `ping` | 연결 상태 확인 |
| `move` | 모터 이동 |
| `motion_start` | 모션 시퀀스 시작 |
| `motion_stop` | 모션 정지 |
| `motion_pause` | 모션 일시정지 |
| `get_files` | 파일 목록 조회 |
| `get_file` | 파일 내용 읽기 |
| `save_file` | 파일 저장 |
| `verify_file` | 파일 검증 |
| `get_motors` | 모터 목록 조회 |
| `get_motor_state` | 모터 상태 조회 |

## 라이선스

cJSON: MIT License (Copyright (c) 2009-2017 Dave Gamble and cJSON contributors)
