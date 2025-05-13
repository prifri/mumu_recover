# mumu_recover

`mumu_recover.exe`는 Netease MuMuPlayer에서 실행되는 AHK 매크로의 안정적인 동작을 위해 자동 복구 및 재실행 기능을 제공하는 도구입니다.

이 프로그램은 다음 두 GitHub 프로젝트의 매크로 기반으로 설계되었습니다:

- 🔗 [Arturo-1212/PTCGPB](https://github.com/Arturo-1212/PTCGPB)
- 🔗 [danilocostaoliveira/PTCGPB (featureSilentPatch)](https://github.com/danilocostaoliveira/PTCGPB/tree/featureSilentPatch)

이들 매크로는 Pokémon TCG Pocket의 자동 리롤 및 패키지 개봉을 자동화하며, `Scripts/1.ahk`, `2.ahk` 등의 구조로 실행됩니다. 그러나 AHK나 MuMuPlayer가 비정상 상태가 되는 경우 수동 개입이 필요했습니다.

`mumu_recover.exe`는 이 문제를 해결하여 **무인 운영**이 가능하도록 돕습니다.

---

## ✅ 주요 기능

- `setting.ini`에서 MuMu 설치 경로 자동 파싱 (`folderPath`)
- 각 인스턴스의 AHK 창, MuMu 창, adb 연결 상태 확인
- 창이 비응답 상태일 경우 자동으로 AHK 및 MuMu 종료 후 재실행
- 로그에 타임스탬프 포함
- `--help`, `-h` 인자를 통한 간단한 도움말 출력

---

## 🧪 사용법

```sh
mumu_recover.exe [인스턴스개수] [매크로폴더경로 (선택)]
