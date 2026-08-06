# Public headers

Headers here define small, testable contracts between source lanes. `EngineMap.h` owns build-specific RVAs/signatures and object offsets; `AimState.h` owns the exact captured reticle-ray layout; `InteractionQuery.h` owns the evidenced candidate-record projection; `FrameObserver.h` plans the only currently authorized runtime hook; `OpenXRBootstrap.h` owns the pure preflight state machine; and `VrMath.h` remains engine-independent. Keep D3D11/OpenXR handles and live game pointers out of pure data and math APIs.
