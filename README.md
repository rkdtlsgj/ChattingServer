# IOCP_Chatting_Server
c++를 이용한 IOCP 섹터방식의 채팅서버<br>
<br>

# 환경
* redis 
* boost library - lockfreeQueue 사용
<br>


# 최적화
* CLFFreeList playerPool , msgPool , g_PacketPool 락경쟁 및 힙할당 최소화를 위해 메모리풀을 구현해서 사용  
* packet의 제대로된 수명관리를 위해 refCount를 이용하여 관리 <br> <img width="370" height="311" alt="image" src="https://github.com/user-attachments/assets/ca11b11f-a1bf-4975-b6b3-c2dfbebedbd3" />

* 테스트중 발생하는 오류를 분석하기 위해 CCrashDump 추가
* tls를 사용한 프로파일러를 구현하여 성능테스트

* 메세지 큐를 이용해서 단일스레드(updateThread)로 메세지처리<br> <img width="359" height="125" alt="image" src="https://github.com/user-attachments/assets/c7dedd13-ce26-49c9-a760-a601274a9fa1" />

* 50 x 50 섹터에서 주변 3 x 3 섹터에만 메시지 전송으로 효율적으로 수정
<br><br><br>



# 테스트
echo 테스트(100명)<br><br> <img width="359" height="125" alt="에코 테스트1" src="https://github.com/user-attachments/assets/f6fadd40-48a3-42a2-b500-f29d7627c11c" />




<br><br>
더미 테스트(3000명)<br>
이동 500ms<br>
채팅 300ms<br>
pool 사용량 체크

<img width="191" height="372" alt="image" src="https://github.com/user-attachments/assets/f9eaf0fc-e0e7-406c-b7bb-6376b11cde6e" />
