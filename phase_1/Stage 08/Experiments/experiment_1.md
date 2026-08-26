# Stage 8 : Experiment 1

Ran the program with btop in the side monitoring stats.

- Started the server
  ![Server Running started state](Stage08_Exp01_CPU_Util_Server_Running.png)
- Connected a client using netcat on port 8002
  Showed signs of increased CPU utilisation on all CPU cores, expecially C5, C6, and C7 (Upto 100% sometimes)
  ![Client Connected to Server](Stage08_Exp01_Client_Connected.png)
- Connected a client using netcat on port 8002 in DEBUG mode
  Same effect on CPU utilisation without DEBUG mode, but considerably less utilization
  ![Client Connected to Server in DEBUG mode](Stage08_Exp01_Client_Connected_DebugMode.png)

## Reasons

- Since epoll is currently in level triggered mode, its notifying all the time because the kernel buffer is free to write to.
  Since we have nothing to write when connection is idle, it will notify everytime.
- CPU utilisation is comparitively lesser because now the CPU takes some time for printing the DEBUG messages.
