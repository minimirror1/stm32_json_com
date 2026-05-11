Behavioral guidelines to reduce common LLM coding mistakes. Merge with project-specific instructions as needed.

**Tradeoff:** These guidelines bias toward caution over speed. For trivial tasks, use judgment.

## 1. Think Before Coding

**Don't assume. Don't hide confusion. Surface tradeoffs.**

Before implementing:
- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them - don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.

## 2. Simplicity First

**Minimum code that solves the problem. Nothing speculative.**

- No features beyond what was asked.
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

Ask yourself: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

## 3. Surgical Changes

**Touch only what you must. Clean up only your own mess.**

When editing existing code:
- Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it - don't delete it.

When your changes create orphans:
- Remove imports/variables/functions that YOUR changes made unused.
- Don't remove pre-existing dead code unless asked.

The test: Every changed line should trace directly to the user's request.

## 4. Goal-Driven Execution

**Define success criteria. Loop until verified.**

Transform tasks into verifiable goals:
- "Add validation" → "Write tests for invalid inputs, then make them pass"
- "Fix the bug" → "Write a test that reproduces it, then make it pass"
- "Refactor X" → "Ensure tests pass before and after"

For multi-step tasks, state a brief plan:
```
1. [Step] → verify: [check]
2. [Step] → verify: [check]
3. [Step] → verify: [check]
```

Strong success criteria let you loop independently. Weak criteria ("make it work") require constant clarification.

---

**These guidelines are working if:** fewer unnecessary changes in diffs, fewer rewrites due to overcomplication, and clarifying questions come before implementation rather than after mistakes.

## 5. Git Branch Strategy

**Keep deployable code stable. Put work on the branch that matches its risk.**

Before choosing a branch:
- Use `master` for deployable stable code only.
- Use `develop` as the integration branch for the next release.
- Use `feature/*` for normal feature work created from `develop`.
- Use `hotfix/*` for urgent production fixes created from `master`.
- Use `release/*` only when a separate release stabilization period is needed.

When merging:
- Merge completed `feature/*` work back into `develop`.
- Merge completed `hotfix/*` work back into both `master` and `develop`.
- Tag releases on `master` using version tags such as `v1.2.0`.

The test: `master` should always be deployable, and every non-master branch should have a clear reason to exist.



# STM32 Binary Communication Library - Agent Guidelines

이 문서는 Binary 통신 라이브러리에서 새로운 `App_*` 함수나 통신 기능을 구현할 때 참고할 규칙입니다.

---

## 1. 아키텍처 원칙: Application Layer 분리

```text
통신 레이어 (binary_com.c)  <->  App 레이어 (device_real.c)
- Binary 패킷 파싱                 - 순수 어플리케이션 로직
- 응답 프레임 구성                 - 하드웨어 제어
- 프로토콜 처리                    - 파일 시스템 접근
```

### App_* 함수 설계 규칙
- 순수 로직만 구현합니다.
- 통신 포맷 파싱/직렬화 코드를 포함하지 않습니다.
- 반환값은 `bool` 또는 `int(count/-1)`를 사용합니다.
- 복잡한 데이터는 out 파라미터로 전달합니다.

---

## 2. Binary 직렬화 규칙

- Wire 포맷은 `binary_com.h` 정의를 따릅니다.
- 멀티바이트 값은 반드시 `read_u16le`/`write_u16le`/`write_u32le` 헬퍼로 처리합니다.
- 구조체 캐스팅으로 endian을 처리하지 않습니다.
- payload 길이는 항상 헤더 `payload_len`과 일치해야 합니다.

---

## 3. App_GetFiles / App_GetMotors 반환 규칙

- 배열 원소 수(count)는 호출자가 제공한 최대치 이내여야 합니다.
- 문자열 필드(`name`, `path`, `content`)는 null-termination을 보장해야 합니다.
- 부모-자식 트리에서는 부모 항목이 항상 먼저 나와야 합니다.

---

## 4. 새로운 App_* 함수 추가 절차

1. `device_hal.h`에 선언 추가
2. `device_real.c`에 `__weak` 스텁/예시 추가
3. `device_mock.c`에 테스트용 구현 추가
4. `binary_com.c`에 명령 핸들러 추가 및 payload 직렬화 구현

---

## 5. 파일 구조

```text
Core/
├── Inc/
│   └── device_hal.h
└── Src/
    ├── device_real.c
    └── device_mock.c

Lib/stm32_json_com/
├── Inc/
│   └── binary_com.h
└── Src/
    └── binary_com.c
```

---

## 6. 체크리스트

- [ ] App 레이어와 통신 레이어가 분리되어 있는가?
- [ ] payload 직렬화 순서/길이가 문서와 일치하는가?
- [ ] LE 변환 헬퍼를 사용했는가?
- [ ] count/길이/경계 검증이 충분한가?
- [ ] mock/weak 구현이 함께 갱신되었는가?
