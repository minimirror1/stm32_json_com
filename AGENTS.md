# STM32 JSON Communication Library - Agent Guidelines

이 문서는 JSON 통신 라이브러리에서 새로운 App_* 함수나 통신 기능을 구현할 때 참고해야 할 규칙입니다.

---

## 1. 아키텍처 원칙: Application Layer 분리

### 핵심 원칙
```
통신 레이어 (json_com.c)     <-->     App 레이어 (device_real.c)
- JSON 파싱                           - 순수 어플리케이션 로직
- JSON 응답 구성                       - 하드웨어 제어
- 프로토콜 처리                        - 파일 시스템 접근
```

### App_* 함수 설계 규칙
- **순수 로직만**: 통신 파싱, 응답 포맷팅 코드 금지
- **단순 반환값**: `bool` (성공/실패) 또는 `int` (개수/-1)
- **출력 파라미터**: 복잡한 데이터는 out 파라미터로 전달
- **__weak 선언**: 개발자가 별도 파일에서 override

### 예시
```c
// 좋은 예: 순수 로직만
bool App_SaveFile(const char *path, const char *content) {
    return SD_WriteFile(path, content) == 0;
}

// 나쁜 예: 통신 관련 코드 포함
bool App_SaveFile(const char *path, const char *content, char *response) {
    if (SD_WriteFile(path, content) == 0) {
        strcpy(response, "{\"status\":\"saved\"}");  // 금지!
        return true;
    }
    return false;
}
```

---

## 2. 트리 구조 표현 (App_GetFiles)

### AppFileInfo 구조체
```c
typedef struct {
    char name[64];        // 파일/폴더 이름
    char path[128];       // 전체 경로
    bool is_directory;    // 폴더 여부
    uint32_t size;        // 파일 크기
    uint8_t depth;        // 깊이: 0=root, 1=하위, 2=하위의 하위...
    int16_t parent_index; // 부모 인덱스 (-1=root)
} AppFileInfo;
```

### 규칙
1. **부모가 먼저**: 배열에서 부모 폴더는 자식보다 앞에 위치해야 함
2. **parent_index**: 부모의 배열 인덱스를 정확히 지정
3. **Icon 자동 생성**: is_directory 기반으로 통신 레이어에서 자동 추가

### 예시
```
Settings/           [0] depth=0, parent=-1
  config.txt        [1] depth=1, parent=0
  Motor/            [2] depth=1, parent=0
    settings.ini    [3] depth=2, parent=2
Log/                [4] depth=0, parent=-1
  boot.txt          [5] depth=1, parent=4
```

---

## 3. 메모리 효율적인 JSON 출력 (중요!)

### 문제
- STM32의 제한된 heap 메모리
- cJSON으로 큰 트리 구조 생성 시 malloc 실패 → HardFault

### 해결책: 스트리밍 방식
```c
// 나쁜 예: cJSON으로 전체 트리 생성 (메모리 부족 위험)
cJSON *tree = BuildFullTree(files, count);  // 많은 malloc 호출
SendResponse(ctx, tree);

// 좋은 예: UART로 직접 스트리밍 출력
UART_SendStringBlocking(ctx->uart, "{\"msg\":\"resp\",...,\"payload\":[");
StreamFileTreeRecursive(ctx, files, count, -1, 0);  // 재귀적 출력
UART_SendStringBlocking(ctx->uart, "]}\n");
```

### 규칙
- 대용량 데이터(파일 목록 등)는 스트리밍 방식 사용
- 작은 응답은 cJSON 사용 가능
- cJSON 사용 시 반드시 NULL 체크

---

## 4. cJSON 사용 시 주의사항

### 함수 존재 여부 확인
현재 cJSON.c에 없는 함수:
- `cJSON_AddArrayToObject` (없음 → `cJSON_CreateArray` + `cJSON_AddItemToObject` 사용)

### NULL 체크 필수
```c
cJSON *obj = cJSON_CreateObject();
if (obj == NULL) return;  // 필수!

cJSON *arr = cJSON_CreateArray();
if (arr == NULL) {
    cJSON_Delete(obj);
    return;
}
```

### 유니코드 이스케이프
```c
// 잘못된 예: 실제 유니코드 문자로 변환됨
#define ICON_FOLDER "\uE8B7"

// 올바른 예: JSON 이스케이프 문자열 (6개의 ASCII 문자)
#define ICON_FOLDER "\\uE8B7"
```

---

## 5. 새로운 App_* 함수 추가 절차

1. **device_hal.h**: 함수 선언 추가
2. **device_real.c**: `__weak` 스텁 + 상세한 예제 주석
3. **device_mock.c**: 테스트용 mock 구현
4. **json_com.c**: Handler 함수에서 App_* 호출 + 응답 구성

### Handler 함수 템플릿
```c
static void HandleNewCommand(JSON_Context *ctx, uint8_t req_src_id, 
                             cJSON *req_payload, bool respond) {
    // 1. 요청에서 파라미터 추출
    const char *param = cJSON_GetStringValue(
        cJSON_GetObjectItem(req_payload, "param"));
    
    // 2. App 함수 호출 (순수 로직)
    bool success = App_NewCommand(param);
    
    // 3. 응답 구성 (통신 레이어에서 처리)
    if (!success) {
        SendError(ctx, req_src_id, "new_cmd", "Failed", respond);
        return;
    }
    
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "status", "done");
    SendSuccess(ctx, req_src_id, "new_cmd", payload, respond);
}
```

---

## 6. 파일 구조

```
Core/
├── Inc/
│   ├── device_hal.h    # App_* 함수 선언, AppFileInfo 등 타입 정의
│   └── json_com.h      # JSON 통신 컨텍스트, 상수
├── Src/
│   ├── device_real.c   # __weak App_* 스텁 (개발자가 override)
│   ├── device_mock.c   # 테스트용 App_* 구현 (USE_MOCK_DEVICE)
│   └── json_com.c      # 통신 레이어 (Handler 함수들)
```

---

## 7. 체크리스트

새 기능 구현 시:
- [ ] App_* 함수가 순수 로직만 포함하는가?
- [ ] 통신 응답 구성은 json_com.c에서 하는가?
- [ ] 대용량 데이터는 스트리밍 방식인가?
- [ ] cJSON 사용 시 NULL 체크 했는가?
- [ ] device_real.c에 상세한 예제 주석이 있는가?
- [ ] device_mock.c에 테스트 구현이 있는가?
