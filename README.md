# NTPLite
A transport-agnostic NTP-based time synchronization client-server tool for resource constrained devices.

Each exchange records four timestamps of format $\{s, ns\}$ which are then converted to integer nanoseconds:

- $t_1$: client transmit
- $t_2$: server receive
- $t_3$: server transmit
- $t_4$: client receive

$$\delta = (t_4 - t_1) - (t_3 - t_2)$$

$$\hat\theta = \frac{(t_2 - t_1) + (t_3 - t_4)}{2}$$

$\delta$ is the round-trip network delay, with the server's processing time removed. $\hat\theta$ is the estimated offset (server minus client), so a positive value means the client is behind. A negative $\delta$ is rejected as invalid.

### Milestones 
- [ ] Alpha release

---

Made with ❤️ by North, with special thanks to [EWS Andromeda](https://github.com/EWS-ANDROMEDA), a SAR robotics team of [Undip Robotic Development Center, Diponegoro University](https://www.instagram.com/robotikundip/).
