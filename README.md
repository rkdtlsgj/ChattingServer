# IOCP_Chatting_Server
c++를 이용한 IOCP 섹터방식의 채팅서버
boost library - lockfreeQueue 사용

간단한 암호화 추가



메세지 큐를 이용해서 단일스레드로 메세지처리



50 x 50 섹터에서 주변 3 x 3 섹터에만 메시지 전송으로 효율적으로 수정



echo 테스트(100명)

<img width="359" height="125" alt="에코 테스트1" src="https://github.com/user-attachments/assets/f6fadd40-48a3-42a2-b500-f29d7627c11c" />


더미 테스트(3000명)
이동 500ms
채팅 300ms

<img width="229" height="119" alt="섹터 테스트" src="https://github.com/user-attachments/assets/1c6ac3b2-76e7-47ec-93d2-7445726b3723" />
