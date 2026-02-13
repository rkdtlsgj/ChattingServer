# IOCP_Chatting_Server
c++를 이용한 IOCP 섹터방식의 채팅서버<br>
<br>

# 환경
* redis 
* boost library - lockfreeQueue 사용

# 개발 목적
* 고성능 windows 네트워크 I/O 모델을 사용해서 수천명의 동시 클라이언트 대상으로 메세지 / 채팅을 처리하는 서버 입니다
* 채팅 브로드캐스트는 맵 전체가 아닌 섹터 기반으로 범위를 제한해 불필요한 네트워크 트래픽을 줄였고, 로그인 검증처럼 상대적으로 느린
  외부 작업은 전용스레드로 IOCP 워커의 처리량저하를 방지했습니다.

# 주요 기능
* IOCPServer 기반 Accept/Recv/Send 완료 이벤트 처리, 워커 스레드 풀에서 병렬 처리
* RecvProc()에서 패킷 타입 디코딩 후 요청별 핸들러(로그인/이동/채팅/하트비트) 라우팅
* 섹터(Zone) 기반 AOI 브로드캐스트 — 50×50 섹터 맵에서 주변 3×3 섹터만 대상으로 채팅 전송
* 외부 작업 오프로딩(Redis) — 로그인 검증을 redisQ로 넘기고 Redis 전용 스레드가 처리 후 결과 반영
* 운영 모니터링 — Accept/Recv/Send TPS, 접속자 수, CPU/메모리 사용량을 주기적으로 출력
* Timeout 관리 — lastRecvTime 기반 비활성 세션을 감지하여 강제 종료

# 시스템 구조
* OnClientJoin/Leave/Recv 이벤트를 받아 내부 처리(JoinProc/LeaveProc/RecvProc)로 위임
* 섹터(Zone) 맵
  sector[y][x].set에 해당 섹터에 존재하는 Player 포인터를 보관
  섹터 이동 시 set 갱신, 채팅 시 주변 섹터에서 대상 수집
* 스레드 구성
  IOCP 워커 스레드(베이스 서버) + RedisThread + HeartbeatThread + MonitorThread

* IOCP 워커스레드가 즉시 처리 ( 섹터이동 / 채팅 / 하트비트 등 짧고 빈번한 작업)
  전용 스레드 로그인 검증처럼 느린 작업은 워커사용을 지양함
  
  
# 최적화
* CLFFreeList playerPool , msgPool , g_PacketPool 락경쟁 및 힙할당 최소화를 위해 메모리풀을 구현해서 사용  
* packet의 제대로된 수명관리를 위해 refCount를 이용하여 관리 <br> <img width="370" height="311" alt="image" src="https://github.com/user-attachments/assets/ca11b11f-a1bf-4975-b6b3-c2dfbebedbd3" />

* 테스트중 발생하는 오류를 분석하기 위해 CCrashDump 추가
* tls를 사용한 프로파일러를 구현하여 성능테스트

* 메세지 큐를 이용해서 단일스레드(updateThread)로 메세지처리<br> <img width="359" height="125" alt="image" src="https://github.com/user-attachments/assets/c7dedd13-ce26-49c9-a760-a601274a9fa1" />

* 50 x 50 섹터에서 주변 3 x 3 섹터에만 메시지 전송으로 효율적으로 수정
<br><br><br>


# 최적화2
* msgPool / 단일 스레드로 처리시 인원이 많아 질 수록 처리속도보다 메세지가 쌓이는 속도가 더 높아 처리가 밀리는 상황이 발생<br><br> <img width="1012" height="346" alt="계속증가" src="https://github.com/user-attachments/assets/a3993d4f-8fea-4a2e-86d6-feae7dc68b1a" />

* 해결방법으로 msgpool / 단일 스레드 방식이 아닌 워커스레드를 이용해서 락을 사용한 메세지 처리로 변경 <br><br> <img width="952" height="156" alt="2" src="https://github.com/user-attachments/assets/c9f8c8fd-4bd9-416d-8885-6a8b233aee42" />

* 구조 변경으로 player의 수명관리 시스템을 변경 Acquire - AddRef -> Release 식으로 포인터 오류 방지
