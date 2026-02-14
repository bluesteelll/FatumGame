# FatumGame — TODO

## Destructible System

- **Optimize fragments: Static until detached.** Currently all fragments spawn as Dynamic (expensive — broadphase + integration every tick even when stationary). Optimization: spawn constrained fragments as Static (zero CPU), switch to Dynamic only when their constraints break. Caveat: Static-Static constraints have zero forces → break detection won't fire. Need to ensure at least one body in each constraint pair is Dynamic (the impacted fragment or its neighbors).
