# Thistle
 
This is a separable packaging for our behavior trees which are classically quite difficult to version control. Hopefully, this will allow us to make some fundamental progress on the problem while also keeping development speed high enough. Thistle intends to make use of ActorComponents rather than Pawns for Assumption of Control as well as allowing queuing of orders from state trees, behavior trees, and mass transparently so that designers don't need to assign individual behavior trees to specific units beforehand, but can instead provide more approachable hints like strategy, faction, and aggression, then let Thistle handle the rest. The goal is to decouple the NPC and NPC AI systems entirely, allowing far cleaner inheritance and easy inclusion after-the-fact. It may also support async AI, necessary for really taking advantage of async physics and networking. TBD.

## Useful references
A few parts of the design of thistle may seem odd. It's natural to assume that the design goals of an AI lib are to make enemies as smart as possible. If you feel this way, I encourage you to watch the following three talks discussing how that caused games you probably enjoyed to almost never ship:

- https://www.youtube.com/watch?v=ueEmiDM94IE
- https://www.youtube.com/watch?v=kda7rz5qFtI
- https://www.youtube.com/watch?v=1xWg54mdQos
