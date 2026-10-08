# ERShorts — 이터널리턴 쇼츠 메이커 (libobs 내장)

**⬇ 다운로드 (Windows, 로그인 불필요):** [최신 버전 ERShorts-windows-x64.zip](https://github.com/rhsung1004-ctrl/ershorts/releases/latest/download/ERShorts-windows-x64.zip)
· [모든 버전 보기](https://github.com/rhsung1004-ctrl/ershorts/releases)

게임하면서 F9 한 번 → 최근 N초 클립 저장 → 9:16 쇼츠로 바로 내보내기까지 하나의 프로그램에서 처리합니다.

## 기능
- **리플레이 버퍼**: libobs `replay_buffer` 출력으로 최근 10~300초를 계속 녹화, F9로 저장 (저장 시 알림음)
- **게임 캡처**: `game_capture` (전체화면 게임 자동 감지 또는 특정 창 지정), 막히면 `monitor_capture`로 전환
- **하드웨어 인코딩**: NVENC → AMF → QSV → x264 순서로 자동 선택, 키프레임 1초
- **미리보기**: libobs가 Qt 창에 직접 렌더링 + 9:16 크롭 가이드 표시
- **클립 목록**: 장면 썸네일·길이·녹화 시각 표시, 이름 바꾸기(F2), 휴지통으로 삭제(Delete), ★ 즐겨찾기 / ★만 보기
- **매드무비 편집기** (클립 여러 개 선택 → "선택한 클립으로 영상 만들기")
  - 여러 클립을 한 타임라인에 이어 붙이기, 자르기(S) / 삭제(Del) / 복제(Ctrl+D) / 드래그로 순서 변경
  - 구간별 배속 0.25x ~ 3x + **속도 램프**(1x↔배속으로 부드럽게 바뀜, 들어갈 때/나올 때, 길이 조절)
  - 효과(줌인·흔들림·흑백·색감 강조·비네팅) — **줌인은 미리보기에서 마우스로 영역을 그려서** 확대 위치·배율 지정
  - **프리즈 프레임**: 구간 끝 장면에서 0.5~2초 멈춤 (흑백·번쩍 선택)
  - **되감기 리플레이**: 선택 구간의 마지막 2~4초를 거꾸로 3배속으로 감은 뒤 0.5배속으로 다시
  - **효과음**: 효과음 파일을 목록에 넣어 두고 현재 위치 / 모든 컷에 한 번에 넣기, 개별 볼륨
  - **이미지(로고·스티커)**: 영상 전체 워터마크 또는 원하는 시간에만 표시, 미리보기에서 끌어 이동·휠로 크기, 불투명도
  - **실행 취소 / 다시 실행** (Ctrl+Z / Ctrl+Y, 최대 200단계)
  - 컷 전환: 화이트 플래시 / 블랙 페이드 / 줌 펀치 / 글리치 ("모든 컷에 적용" 가능)
  - BGM: 시작 위치, 음악·게임 소리 볼륨 따로, 끝부분 페이드 아웃
  - **음악 자동 줄이기(덕킹)**: 킬 사운드·스킬음처럼 게임 소리가 커지면 음악을 잠깐 낮춤 (세기 3단계, 미리보기에도 반영)
  - **BPM 자동 감지** + 타임라인에 비트 표시, 구간 끝을 끌면 비트에 자석처럼 붙음,
    "모든 컷을 비트에 맞추기", "선택 구간을 비트마다 자르기(B)" — 컷 간격 매 박/2박/4박/8박
  - **비트 효과**: 비트마다 줌 펄스 / 흔들림 (세기 3단계, 매 박·2박·4박)
  - **타임라인 썸네일** + **확대/축소** (Ctrl+휠, 최대 64배, 재생 중 화면이 따라감)
  - **제목 띠 레이아웃** (기본값): 위/아래 단색 띠에 제목·부제·아래 문구, 가운데는 게임 화면을 넓게 잘라 배치
    (여백 위·아래 / 위만 / 아래만 / 없음 선택, 띠 높이·색, 글자 크기·색, 영상 확대·세로 위치 조절, 긴 글씨 자동 축소)
  - 자막: 직접 입력(여러 줄), 시간·크기·색상·세로 위치·반투명 박스
  - **미리보기에서 직접 조정**: 자막과 가운데 게임 화면을 위아래로 끌어 위치 변경, 게임 화면 위에서 휠로 확대
  - **스타일 템플릿**: 화면 구성·자막 모양·비트 효과·소리 설정을 이름 붙여 저장 → 다른 영상에 바로 적용,
    "기본 스타일"로 지정하면 새 영상에 자동 적용
  - 9:16 실시간 미리보기 (배속·줌·전환·자막·인트로가 그대로 보이고 BGM도 같이 재생)
  - 레이아웃: 가운데 크롭 / 가운데 크롭 + 미니맵 / 원본 + 흐린 배경
  - 프로젝트는 `동영상/ERShorts/projects/*.json`에 자동 저장 → "저장된 프로젝트 열기"로 이어서 편집
  - 내보내기: ffmpeg 한 번으로 렌더링, 프레임 단위로 길이를 맞춰 컷이 비트에서 밀리지 않음
  - **빠른 미리보기 내보내기**: 540×960 / 30fps / 빠른 인코딩으로 확인용 영상 (shorts/preview 에 덮어쓰기)

## 프로젝트 구조
```
src/
  main.cpp             앱 진입점
  ObsCore.*            libobs 초기화, 캡처/오디오 소스, 인코더, 리플레이 버퍼
  PreviewWidget.*      obs_display 기반 미리보기 + 크롭 가이드
  GlobalHotkey.*       게임 중에도 동작하는 전역 단축키 (RegisterHotKey)
  EditProject.*        매드무비 프로젝트(클립, 구간, 전환, BGM/비트, 인트로/아웃트로, 자막) + JSON 저장
  BeatDetector.*       BGM의 BPM·첫 박 자동 감지 (온셋 + 자기상관 + 콤 필터)
  ThumbnailCache.*     타임라인용 장면 썸네일 생성/캐시 (ffmpeg, 0.5초 간격)
  TimelineWidget.*     결과 시간 기준 타임라인 (영상/음악·비트/자막 줄)
  EditorWindow.*       편집기 창 (미리보기, 클립/구간/음악/자막/인트로/내보내기 탭)
  ShortsExporter.*     프로젝트 전체를 ffmpeg 한 번에 렌더링 (9:16)
  MainWindow.*         녹화 + 클립 목록 UI
```

## exe 만들기 — GitHub Actions (권장, PC에 개발 도구 설치 불필요)

1. GitHub에서 새 저장소(Repository)를 만듭니다 (비공개도 가능).
2. 이 폴더의 파일을 **`.github` 폴더까지 포함해서** 그대로 올립니다.
   - 웹에서 올릴 때: 저장소 페이지 → "Add file" → "Upload files" 에 폴더째 끌어다 놓기
3. 저장소의 **Actions** 탭으로 가면 "Windows 빌드"가 자동으로 실행됩니다.
   (안 보이면 "Windows 빌드" 선택 → "Run workflow")
4. 첫 빌드는 OBS까지 같이 빌드해서 **30~40분** 걸립니다. 다음부터는 캐시 덕분에 5~10분.
5. 끝나면 실행 기록 아래쪽 **Artifacts** 의 `ERShorts-windows-x64` 를 내려받아 압축을 풀고,
   맨 위의 **ERShorts.exe** 를 실행하면 됩니다.

`v0.1.0` 처럼 v로 시작하는 태그를 올리면 Releases 페이지에도 zip이 자동으로 올라갑니다.

## 직접 빌드 (Windows 10/11, x64)

### 1. 준비물
- Visual Studio 2022 (C++ 데스크톱 개발)
- CMake 3.24+
- Qt 6.6 이상 (MSVC 2022 64-bit) + **Qt Multimedia** 모듈 (Qt 설치 시 추가 선택)
- ffmpeg.exe (gyan.dev 등의 Windows 빌드, NVENC 포함)

### 2. OBS Studio(libobs) 빌드
```powershell
git clone --recursive https://github.com/obsproject/obs-studio.git
cd obs-studio
git checkout <최신 안정 버전 태그>   # 예: 31.x
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo
# libobs 헤더/CMake 설정 파일 설치 (SDK 용)
cmake --install build_x64 --config RelWithDebInfo --prefix C:/obs-sdk
```

### 3. ERShorts 빌드
```powershell
cd ERShorts
cmake -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.8.0/msvc2022_64;C:/obs-sdk"
cmake --build build --config RelWithDebInfo
```

### 4. 실행 폴더 구성
(GitHub Actions 빌드는 이 과정을 자동으로 해 줍니다 — `.github/workflows/build-windows.yml` 참고)

libobs는 실행 파일 기준 `../../data`, `../../obs-plugins/64bit` 에서 리소스를 찾습니다.
OBS 빌드 결과(`obs-studio/build_x64/rundir/RelWithDebInfo`)를 복사한 뒤 아래처럼 맞추세요.

```
ERShorts/
├─ bin/64bit/
│   ├─ ERShorts.exe
│   ├─ obs.dll, libobs-d3d11.dll, libobs-winrt.dll, w32-pthreads.dll, avcodec-*.dll ... (OBS rundir에서)
│   ├─ Qt6*.dll   (windeployqt ERShorts.exe 로 생성)
│   └─ ffmpeg.exe
├─ data/
│   ├─ libobs/                       (OBS rundir에서)
│   └─ obs-plugins/
│       ├─ win-capture/  (graphics-hook64.dll, inject-helper64.exe 등 포함)
│       ├─ win-wasapi/ obs-ffmpeg/ obs-x264/ obs-nvenc/ ...
└─ obs-plugins/64bit/
    ├─ win-capture.dll   win-wasapi.dll   obs-ffmpeg.dll
    ├─ obs-x264.dll      obs-nvenc.dll    obs-qsv11.dll
    └─ (UI용 플러그인 frontend-tools, obs-websocket 등은 필요 없음 — 넣으면 로드 실패 로그만 남음)
```

## 사용법
1. 실행하면 리플레이 버퍼가 자동으로 시작됩니다 (설정에서 끌 수 있음)
2. 이터널리턴을 **창 모드 또는 테두리 없는 창 모드**로 실행 → "창 캡처(권장)"가 게임 창을 자동으로 찾아 연결
3. 멋진 장면 직후 **F9** → 상태 표시가 "✔ 클립 저장됨"으로 바뀌고 `동영상/ERShorts/clips`에 저장
4. 클립 목록 정리: 우클릭 메뉴 또는 버튼으로 **★ 즐겨찾기 / 이름 바꾸기(F2) / 휴지통으로 삭제(Delete)**
5. 영상에 넣을 클립들을 **Ctrl/Shift로 여러 개 선택** → "선택한 클립으로 영상 만들기" (녹화된 순서대로 붙음)
6. **음악** 탭 → 음악 파일 선택 → BPM 자동 감지 (노란 세로선 = 비트), 필요하면 "음악 자동으로 줄이기" 켜기
7. **구간·효과** 탭 / 타임라인
   - 필요 없는 부분: 재생 위치에서 **S**로 자르고 **Delete**
   - 하이라이트 장면: 구간 선택 → 0.5x 슬로우 + 줌인, 전환은 줌 펀치나 글리치
   - 빠른 컷 연출: 구간 선택 후 **B** (비트마다 자르기)
   - 마무리: 음악 탭의 **모든 컷을 비트에 맞추기**
8. **화면 구성** 탭에서 제목·부제·아래 문구, **자막** 탭에서 자막 넣기
   (미리보기에서 자막·게임 화면을 끌어 위치 조정)
9. 마음에 드는 모양이 나오면 위쪽 **🎨 스타일 → 현재 스타일 저장…** (기본 스타일로 지정하면 다음 영상에 자동 적용)
10. **내보내기** 탭 → **쇼츠로 내보내기** → `동영상/ERShorts/shorts`

미니맵 위치는 해상도/UI 배율에 따라 다를 수 있으니 `EditProject.h`의 `minimapRect`(원본 대비 비율)를 본인 화면에 맞게 조정하세요.

## 라이선스
ERShorts는 **GNU GPL v3.0** (또는 이후 버전)으로 배포되는 자유 소프트웨어입니다. 전문은 [LICENSE](LICENSE)를 보세요.
누구나 사용·수정·재배포할 수 있지만, 수정본을 배포할 때도 같은 GPL 조건으로 소스 코드를 공개해야 합니다.

포함된 오픈소스 구성요소(OBS Studio/libobs — GPL-2.0+, Qt 6 — LGPL-3.0, FFmpeg — GPL-3.0,
Visual C++ 런타임)의 저작권과 라이선스는 [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt)와
[licenses/](licenses) 폴더에 정리되어 있습니다.

## 면책 / 안내
- **비공식 도구**: ERShorts는 개인이 만든 팬 제작 도구로, 이터널리턴(Eternal Return) 및 개발사 님블뉴런(Nimble Neuron),
  OBS Project와 관련이 없고 이들의 승인이나 후원을 받지 않았습니다.
- **보증 없음**: 이 프로그램은 "있는 그대로" 제공되며, 사용으로 생긴 문제(데이터 손실, 계정 문제 등)에 대해 제작자는 책임지지 않습니다.
- **게임 이용**: ERShorts는 화면을 캡처할 뿐 게임 파일이나 게임 메모리를 읽거나 바꾸지 않습니다. 기본값인 "창 캡처"는 게임
  프로세스에 끼어들지도 않습니다. 그래도 게임 이용 약관 준수는 사용자 책임입니다.
- **개인정보**: ERShorts는 어떤 정보도 외부로 보내지 않습니다. 녹화 파일, 프로젝트, 로그는 모두 내 PC에만 저장됩니다.
- **실행 경고**: 코드 서명이 없는 개인 빌드라 처음 실행할 때 "Windows의 PC 보호" 경고가 뜰 수 있습니다. "추가 정보 → 실행"을 누르세요.
- **음악·글꼴 저작권**: 영상에 쓰는 BGM과 글꼴은 각각의 이용 조건을 확인하세요. 프로그램에는 음악이나 글꼴 파일이 들어 있지 않습니다.

## 다음 단계 아이디어
- 킬 알림 영역 OpenCV 템플릿 매칭 → 자동 클립 저장
- 클립 간 겹치는 전환(크로스페이드, 와이프)
