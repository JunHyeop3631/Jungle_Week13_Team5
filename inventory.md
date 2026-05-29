# KraftonEngine 진입점 색인 (inventory)

> 수집 방식: grep 결과로 **확인된 것만** 기록. 기능 명세·추측 없음.
> 검색 범위: `KraftonEngine/Source/**` 만. `KraftonEngine/ThirdParty/**`(sol2, ImGui, RmlUi, fmod, lua 등)는 외부 라이브러리이므로 전수 제외.
> 루트에 README 없음 (`**/README*` 0건).
> 경로는 리포 기준 상대경로(앞에 `KraftonEngine\Source\` 생략 없이 그대로 기재).

---

## 색인 A — 서브시스템 / 매니저 클래스

규칙: `class` 선언 중 이름이 `*System / *Manager / *Subsystem / *Engine / *Renderer / *Component / *Service` 로 끝나는 것.
원시 매칭(215건)에서 **전방선언(`class X;`)·friend·멤버참조·enum** 은 제외하고 **실제 클래스 선언만** 추림.
`*Subsystem`, `*Service` 로 끝나는 클래스명은 **0건**(디렉터리 `Editor/Subsystem`은 있으나 클래스명은 아님).
`.cpp` 열: 동명 `.cpp` 파일 존재 여부 — **아래 77개 전부 존재(✓)**.

### A1. 매니저·서브시스템·엔진·렌더러 (비 Component) — 39개

| 클래스 | 헤더:라인 | .cpp |
|---|---|---|
| UEngine | Engine\Runtime\Engine.h:18 | ✓ |
| UGameEngine | Engine\Runtime\GameEngine.h:9 | ✓ |
| UEditorEngine | Editor\EditorEngine.h:27 | ✓ |
| UObjViewerEngine | ObjViewer\ObjViewerEngine.h:12 | ✓ |
| FRenderer | Engine\Render\Pipeline\Renderer.h:17 | ✓ |
| InputSystem | Engine\Input\InputSystem.h:60 | ✓ |
| UParticleSystem | Engine\Particles\ParticleSystem.h:125 | ✓ |
| FOverlayStatSystem | Editor\Subsystem\OverlayStatSystem.h:25 | ✓ |
| UObjectManager | Engine\Object\Object.h:167 | ✓ (Object.cpp) |
| FResourceManager | Engine\Resource\ResourceManager.h:15 | ✓ |
| FMaterialManager | Engine\Materials\MaterialManager.h:47 | ✓ |
| FMeshManager | Engine\Mesh\MeshManager.h:55 | ✓ |
| FShaderManager | Engine\Render\Shader\ShaderManager.h:291 | ✓ |
| FMeshBufferManager | Engine\Render\Resource\MeshBufferManager.h:9 | ✓ |
| FSamplerStateManager | Engine\Render\RenderState\SamplerStateManager.h:6 | ✓ |
| FRasterizerStateManager | Engine\Render\RenderState\RasterizerStateManager.h:6 | ✓ |
| FBlendStateManager | Engine\Render\RenderState\BlendStateManager.h:6 | ✓ |
| FDepthStencilStateManager | Engine\Render\RenderState\DepthStencilStateManager.h:6 | ✓ |
| FAudioManager | Engine\Audio\AudioManager.h:7 | ✓ |
| FAnimationManager | Engine\Animation\AnimationManager.h:20 | ✓ |
| FAnimGraphManager | Engine\Animation\Graph\AnimGraphManager.h:11 | ✓ |
| FSkeletonManager | Engine\Animation\Skeleton\SkeletonManager.h:10 | ✓ |
| FCameraShakeManager | Engine\CameraShake\CameraShakeManager.h:7 | ✓ |
| APlayerCameraManager | Engine\GameFramework\Camera\PlayerCameraManager.h:20 | ✓ |
| FParticleSystemManager | Engine\Particles\ParticleSystemManager.h:10 | ✓ |
| FFloatCurveManager | Engine\FloatCurve\FloatCurveManager.h:7 | ✓ |
| FLuaScriptManager | Engine\Lua\LuaScriptManager.h:12 | ✓ |
| UUIManager | Engine\UI\UIManager.h:84 | ✓ |
| FTickManager | Engine\Core\TickFunction.h:112 | ✓ (TickFunction.cpp) |
| FStatManager | Engine\Profiling\Stats\Stats.h:71 | ✓ (Stats.cpp) |
| FLogManager | Engine\Core\Logging\Log.h:25 | ✓ (Log.cpp) |
| FNotificationManager | Engine\Core\Logging\Notification.h:35 | ✓ (Notification.cpp) |
| FSceneSaveManager | Engine\Serialization\SceneSaveManager.h:38 | ✓ |
| FSelectionManager | Editor\Selection\SelectionManager.h:11 | ✓ |
| FAssetEditorManager | Editor\UI\Asset\AssetEditorManager.h:12 | ✓ |
| FEditorPropertyRenderer | Editor\UI\Panel\EditorPropertyRenderer.h:34 | ✓ |
| FEditorTextureManager | Editor\UI\Util\EditorTextureManager.h:9 | ✓ |
| FEditorMeshThumbnailManager | Editor\UI\Util\EditorMeshThumbnailManager.h:36 | ✓ |
| FEditorMaterialThumbnailManager | Editor\UI\Util\EditorMaterialThumbnailManager.h:32 | ✓ |

### A2. Component — 38개

| 클래스 | 헤더:라인 | .cpp |
|---|---|---|
| UActorComponent | Engine\Component\ActorComponent.h:13 | ✓ |
| USceneComponent | Engine\Component\SceneComponent.h:13 | ✓ |
| UPrimitiveComponent | Engine\Component\PrimitiveComponent.h:59 | ✓ |
| UMeshComponent | Engine\Component\MeshComponent.h:9 | ✓ |
| UShapeComponent | Engine\Component\ShapeComponent.h:11 | ✓ |
| USphereComponent | Engine\Component\Shape\SphereComponent.h:10 | ✓ |
| UCapsuleComponent | Engine\Component\Shape\CapsuleComponent.h:10 | ✓ |
| UBoxComponent | Engine\Component\Shape\BoxComponent.h:10 | ✓ |
| UStaticMeshComponent | Engine\Component\Primitive\StaticMeshComponent.h:20 | ✓ |
| USkinnedMeshComponent | Engine\Component\Primitive\SkinnedMeshComponent.h:20 | ✓ |
| USkeletalMeshComponent | Engine\Component\Primitive\SkeletalMeshComponent.h:17 | ✓ |
| UBillboardComponent | Engine\Component\Primitive\BillboardComponent.h:13 | ✓ |
| UCylindricalBillboardComponent | Engine\Component\Primitive\CylindricalBillboardComponent.h:8 | ✓ |
| USubUVComponent | Engine\Component\Primitive\SubUVComponent.h:11 | ✓ |
| UTextRenderComponent | Engine\Component\Primitive\TextRenderComponent.h:38 | ✓ |
| UDecalComponent | Engine\Component\Primitive\DecalComponent.h:15 | ✓ |
| UHeightFogComponent | Engine\Component\Primitive\HeightFogComponent.h:10 | ✓ |
| UParticleSystemComponent | Engine\Component\Particle\ParticleSystemComponent.h:39 | ✓ |
| ULightComponent | Engine\Component\Light\LightComponent.h:8 | ✓ |
| UAmbientLightComponent | Engine\Component\Light\AmbientLightComponent.h:8 | ✓ |
| UDirectionalLightComponent | Engine\Component\Light\DirectionalLightComponent.h:9 | ✓ |
| UPointLightComponent | Engine\Component\Light\PointLightComponent.h:8 | ✓ |
| USpotLightComponent | Engine\Component\Light\SpotLightComponent.h:8 | ✓ |
| UCameraComponent | Engine\Component\Camera\CameraComponent.h:22 | ✓ |
| UCineCameraComponent | Engine\Component\Camera\CineCameraComponent.h:16 | ✓ |
| USpringArmComponent | Engine\Component\Camera\SpringArmComponent.h:22 | ✓ |
| UMovementComponent | Engine\Component\Movement\MovementComponent.h:16 | ✓ |
| UCharacterMovementComponent | Engine\Component\Movement\CharacterMovementComponent.h:28 | ✓ |
| UFloatingPawnMovementComponent | Engine\Component\Movement\FloatingPawnMovementComponent.h:12 | ✓ |
| UProjectileMovementComponent | Engine\Component\Movement\ProjectileMovementComponent.h:17 | ✓ |
| URotatingMovementComponent | Engine\Component\Movement\RotatingMovementComponent.h:11 | ✓ |
| UPendulumMovementComponent | Engine\Component\Movement\PendulumMovementComponent.h:11 | ✓ |
| UInputComponent | Engine\Component\Input\InputComponent.h:25 | ✓ |
| UActionComponent | Engine\Component\Input\ActionComponent.h:10 | ✓ |
| ULuaScriptComponent | Engine\Component\Script\LuaScriptComponent.h:13 | ✓ |
| UGizmoComponent | Engine\Component\Debug\GizmoComponent.h:24 | ✓ |
| UBoneDebugComponent | Engine\Component\Debug\BoneDebugComponent.h:16 | ✓ |
| UTemporaryBoneAnimatorComponent | Engine\Component\Debug\TemporaryBoneAnimatorComponent.h:20 | ✓ |

**A 소계: 77개 (A1 39 + A2 38), 전부 동명 .cpp 존재.**

---

## 색인 B — 엔진 생명주기 훅 (메서드 정의)

규칙: `.cpp` 내 `Class::(Init|Initialize|Startup|Update|Tick|Render|Draw|Shutdown|Destroy)(` 형태의 **out-of-line 정의**.
원시 매칭 173건에서 **호출부(`Super::Tick(...)`, `UEngine::Init(...)` 등)·주석 29건 제외 → 정의 144건**.
`Startup` / `Draw` 로 끝나는 정의는 0건.

### B1. 엔진/루프 코어
| 클래스::메서드 | 경로:라인 |
|---|---|
| UEngine::Init | Engine\Runtime\Engine.cpp:45 |
| UEngine::Shutdown | Engine\Runtime\Engine.cpp:85 |
| UEngine::Tick | Engine\Runtime\Engine.cpp:120 |
| UEngine::Render | Engine\Runtime\Engine.cpp:141 |
| UGameEngine::Init | Engine\Runtime\GameEngine.cpp:18 |
| UGameEngine::Shutdown | Engine\Runtime\GameEngine.cpp:47 |
| UGameEngine::Tick | Engine\Runtime\GameEngine.cpp:66 |
| UEditorEngine::Init | Editor\EditorEngine.cpp:90 |
| UEditorEngine::Shutdown | Editor\EditorEngine.cpp:153 |
| UEditorEngine::Tick | Editor\EditorEngine.cpp:181 |
| UObjViewerEngine::Init | ObjViewer\ObjViewerEngine.cpp:12 |
| UObjViewerEngine::Shutdown | ObjViewer\ObjViewerEngine.cpp:46 |
| UObjViewerEngine::Tick | ObjViewer\ObjViewerEngine.cpp:62 |
| FEngineLoop::Init | Engine\Runtime\EngineLoop.cpp:15 |
| FEngineLoop::Shutdown | Engine\Runtime\EngineLoop.cpp:77 |

### B2. 플랫폼
| 클래스::메서드 | 경로:라인 |
|---|---|
| FWindowsApplication::Init | Engine\Platform\WindowsApplication.cpp:100 |
| FWindowsApplication::Destroy | Engine\Platform\WindowsApplication.cpp:158 |
| FWindowsWindow::Initialize | Engine\Platform\WindowsWindow.cpp:3 |
| FDirectoryWatcher::Initialize | Engine\Platform\DirectoryWatcher.cpp:9 |
| FDirectoryWatcher::Shutdown | Engine\Platform\DirectoryWatcher.cpp:23 |

### B3. GameFramework (World/Level/Actor)
| 클래스::메서드 | 경로:라인 |
|---|---|
| UWorld::Tick | Engine\GameFramework\World.cpp:342 |
| ULevel::Tick | Engine\GameFramework\Level.cpp:65 |
| AActor::Tick | Engine\GameFramework\AActor.cpp:233 |
| ACharacter::Tick | Engine\GameFramework\Pawn\Character.cpp:96 |

### B4. 매니저/서브시스템
| 클래스::메서드 | 경로:라인 |
|---|---|
| FSelectionManager::Init | Editor\Selection\SelectionManager.cpp:11 |
| FSelectionManager::Shutdown | Editor\Selection\SelectionManager.cpp:36 |
| FSelectionManager::Tick | Editor\Selection\SelectionManager.cpp:214 |
| FAudioManager::Initialize | Engine\Audio\AudioManager.cpp:6 |
| FAudioManager::Shutdown | Engine\Audio\AudioManager.cpp:28 |
| FAudioManager::Tick | Engine\Audio\AudioManager.cpp:63 |
| UUIManager::Initialize | Engine\UI\UIManager.cpp:544 |
| UUIManager::Shutdown | Engine\UI\UIManager.cpp:581 |
| UUIManager::Render | Engine\UI\UIManager.cpp:762 |
| FLuaScriptManager::Initialize | Engine\Lua\LuaScriptManager.cpp:264 |
| FLuaScriptManager::Shutdown | Engine\Lua\LuaScriptManager.cpp:323 |
| FShaderManager::Initialize | Engine\Render\Shader\ShaderManager.cpp:26 |
| FMeshBufferManager::Initialize | Engine\Render\Resource\MeshBufferManager.cpp:4 |
| FTickManager::Tick | Engine\Core\TickFunction.cpp:43 |
| FLogManager::Initialize | Engine\Core\Logging\Log.cpp:58 |
| FLogManager::Shutdown | Engine\Core\Logging\Log.cpp:67 |
| FNotificationManager::Tick | Engine\Core\Logging\Notification.cpp:9 |
| InputSystem::Tick | Engine\Input\InputSystem.cpp:4 |
| FEditorTextureManager::Initialize | Editor\UI\Util\EditorTextureManager.cpp:36 |
| FEditorTextureManager::Shutdown | Editor\UI\Util\EditorTextureManager.cpp:42 |
| FEditorMeshThumbnailManager::Initialize | Editor\UI\Util\EditorMeshThumbnailManager.cpp:14 |
| FEditorMeshThumbnailManager::Shutdown | Editor\UI\Util\EditorMeshThumbnailManager.cpp:19 |
| FEditorMeshThumbnailManager::Tick | Editor\UI\Util\EditorMeshThumbnailManager.cpp:99 |
| FEditorMaterialThumbnailManager::Initialize | Editor\UI\Util\EditorMaterialThumbnailManager.cpp:14 |
| FEditorMaterialThumbnailManager::Shutdown | Editor\UI\Util\EditorMaterialThumbnailManager.cpp:19 |
| FEditorMaterialThumbnailManager::Tick | Editor\UI\Util\EditorMaterialThumbnailManager.cpp:81 |
| FAssetEditorManager::Tick | Editor\UI\Asset\AssetEditorManager.cpp:8 |
| FAssetEditorManager::Render | Editor\UI\Asset\AssetEditorManager.cpp:21 |

### B5. 뷰포트 / 뷰포트 클라이언트
| 클래스::메서드 | 경로:라인 |
|---|---|
| FViewport::Initialize | Engine\Viewport\Viewport.cpp:10 |
| FLevelViewportLayout::Initialize | Editor\Viewport\Level\FLevelViewportLayout.cpp:112 |
| FEditorViewportClient::Initialize | Editor\Viewport\EditorViewportClient.cpp:76 |
| FEditorViewportClient::Tick | Editor\Viewport\EditorViewportClient.cpp:189 |
| FObjViewerViewportClient::Initialize | ObjViewer\ObjViewerViewportClient.cpp:12 |
| FObjViewerViewportClient::Tick | ObjViewer\ObjViewerViewportClient.cpp:66 |
| FStaticMeshEditorViewportClient::Initialize | Editor\Viewport\Asset\StaticMeshEditorViewportClient.cpp:14 |
| FStaticMeshEditorViewportClient::Tick | Editor\Viewport\Asset\StaticMeshEditorViewportClient.cpp:90 |
| FParticleSystemEditorViewportClient::Initialize | Editor\Viewport\Asset\ParticleSystemEditorViewportClient.cpp:15 |
| FParticleSystemEditorViewportClient::Tick | Editor\Viewport\Asset\ParticleSystemEditorViewportClient.cpp:108 |
| FMeshEditorViewportClient::Initialize | Editor\Viewport\Asset\MeshEditorViewportClient.cpp:20 |
| FMeshEditorViewportClient::Tick | Editor\Viewport\Asset\MeshEditorViewportClient.cpp:130 |
| FViewportToolbar::Render | Editor\UI\Toolbar\ViewportToolbar.cpp:145 |

### B6. 에디터 패널 / 위젯
| 클래스::메서드 | 경로:라인 |
|---|---|
| FEditorWidget::Initialize | Editor\UI\EditorWidget.cpp:3 |
| FEditorMainPanel::Render | Editor\UI\EditorMainPanel.cpp:124 |
| FEditorMainPanel::Update | Editor\UI\EditorMainPanel.cpp:728 |
| FEditorSceneWidget::Initialize | Editor\UI\Panel\EditorSceneWidget.cpp:8 |
| FEditorSceneWidget::Render | Editor\UI\Panel\EditorSceneWidget.cpp:13 |
| FEditorPropertyWidget::Render | Editor\UI\Panel\EditorPropertyWidget.cpp:250 |
| FEditorStatWidget::Render | Editor\UI\Panel\EditorStatWidget.cpp:11 |
| FEditorControlWidget::Render | Editor\UI\Panel\EditorControlWidget.cpp:8 |
| FEditorMaterialInspector::Render | Editor\UI\Panel\EditorMaterialInspector.cpp:18 |
| FEditorViewportWidget::Render | Editor\UI\Panel\EditorViewportWidget.cpp:21 |
| EditorWorldSettingsWidget::Render | Editor\UI\Panel\EditorWorldSettingsWidget.cpp:9 |
| EditorProjectSettingsWidget::Render | Editor\UI\Panel\EditorProjectSettingsWidget.cpp:8 |
| FEditorPlayToolbarWidget::Initialize | Editor\UI\Panel\EditorPlayToolbarWidget.cpp:9 |
| FEditorPlayToolbarWidget::Render | Editor\UI\Panel\EditorPlayToolbarWidget.cpp:22 |
| FEditorConsoleWidget::Initialize | Editor\UI\Panel\EditorConsoleWidget.cpp:190 |
| FEditorConsoleWidget::Shutdown | Editor\UI\Panel\EditorConsoleWidget.cpp:287 |
| FEditorConsoleWidget::Render | Editor\UI\Panel\EditorConsoleWidget.cpp:297 |
| EditorShadowMapDebugWidget::Render | Editor\UI\Debug\EditorShadowMapDebugWidget.cpp:259 |
| FEditorAnimationDebugWidget::Render | Editor\UI\Debug\EditorAnimationDebugWidget.cpp:156 |
| FEditorContentBrowserWidget::Initialize | Editor\UI\ContentBrowser\ContentBrowser.cpp:109 |
| FEditorContentBrowserWidget::Render | Editor\UI\ContentBrowser\ContentBrowser.cpp:130 |
| ContentBrowserElement::Render | Editor\UI\ContentBrowser\ContentBrowserElement.cpp:397 |
| EditorDragSource::Render | Editor\UI\Util\EditorDragSource.cpp:4 |
| FObjViewerPanel::Render | ObjViewer\ObjViewerPanel.cpp:39 |
| FObjViewerPanel::Update | ObjViewer\ObjViewerPanel.cpp:55 |
| UUserWidget::Initialize | Engine\UI\UserWidget.cpp:8 |

### B7. 에셋 에디터 위젯
| 클래스::메서드 | 경로:라인 |
|---|---|
| FMaterialEditorWidget::Tick | Editor\UI\Asset\Material\MaterialEditorWidget.cpp:134 |
| FMaterialEditorWidget::Render | Editor\UI\Asset\Material\MaterialEditorWidget.cpp:150 |
| FStaticMeshEditorWidget::Tick | Editor\UI\Asset\Mesh\StaticMeshEditorWidget.cpp:136 |
| FStaticMeshEditorWidget::Render | Editor\UI\Asset\Mesh\StaticMeshEditorWidget.cpp:152 |
| FMeshEditorWidget::Tick | Editor\UI\Asset\Mesh\MeshEditorWidget.cpp:282 |
| FMeshEditorWidget::Render | Editor\UI\Asset\Mesh\MeshEditorWidget.cpp:327 |
| FParticleSystemEditorWidget::Tick | Editor\UI\Asset\Particle\ParticleSystemEditorWidget.cpp:1419 |
| FParticleSystemEditorWidget::Render | Editor\UI\Asset\Particle\ParticleSystemEditorWidget.cpp:1469 |
| FFloatCurveEditorWidget::Render | Editor\UI\Asset\Curve\FloatCurveEditorWidget.cpp:190 |
| FCameraShakeEditorWidget::Render | Editor\UI\Asset\CameraShake\CameraShakeEditorWidget.cpp:362 |
| FAnimGraphEditorWidget::Render | Editor\UI\Asset\Animation\AnimGraphEditorWidget.cpp:584 |
| FAnimationTimelinePanel::Render | Editor\UI\Asset\Animation\AnimationTimelinePanel.cpp:608 |
| FAnimationTransportBar::Render | Editor\UI\Asset\Animation\AnimationTransportBar.cpp:51 |
| FAnimSequencePropertyPanel::Render | Editor\UI\Asset\Animation\AnimSequencePropertyPanel.cpp:75 |
| FAnimMontagePropertyPanel::Render | Editor\UI\Asset\Animation\AnimMontagePropertyPanel.cpp:272 |

### B8. 애니메이션 런타임 (AnimNode / State / Montage)
| 클래스::메서드 | 경로:라인 |
|---|---|
| FAnimNode_StateMachine::Initialize | Engine\Animation\Nodes\AnimNode_StateMachine.cpp:46 |
| FAnimNode_StateMachine::Update | Engine\Animation\Nodes\AnimNode_StateMachine.cpp:70 |
| FAnimNode_Slot::Initialize | Engine\Animation\Nodes\AnimNode_Slot.cpp:8 |
| FAnimNode_Slot::Update | Engine\Animation\Nodes\AnimNode_Slot.cpp:19 |
| FAnimNode_SequencePlayer::Update | Engine\Animation\Nodes\AnimNode_SequencePlayer.cpp:18 |
| FAnimNode_Root::Initialize | Engine\Animation\Nodes\AnimNode_Root.cpp:6 |
| FAnimNode_Root::Update | Engine\Animation\Nodes\AnimNode_Root.cpp:21 |
| FAnimNode_LayeredBlendPerBone::Initialize | Engine\Animation\Nodes\AnimNode_LayeredBlendPerBone.cpp:11 |
| FAnimNode_LayeredBlendPerBone::Update | Engine\Animation\Nodes\AnimNode_LayeredBlendPerBone.cpp:23 |
| FAnimNode_BlendListByEnum::Initialize | Engine\Animation\Nodes\AnimNode_BlendListByEnum.cpp:10 |
| FAnimNode_BlendListByEnum::Update | Engine\Animation\Nodes\AnimNode_BlendListByEnum.cpp:53 |
| UAnimState::Tick | Engine\Animation\StateMachine\AnimState.cpp:44 |
| UAnimMontageInstance::Tick | Engine\Animation\Montage\AnimMontageInstance.cpp:154 |

### B9. 렌더 / 컬링 / 버퍼 / 섀도우
| 클래스::메서드 | 경로:라인 |
|---|---|
| FRenderer::Render | Engine\Render\Pipeline\Renderer.cpp:85 |
| FRenderPassPipeline::Initialize | Engine\Render\RenderPass\RenderPassPipeline.cpp:8 |
| FConstantBuffer::Update | Engine\Render\Resource\Buffer.cpp:132 |
| FDynamicVertexBuffer::Update | Engine\Render\Resource\Buffer.cpp:258 |
| FDynamicIndexBuffer::Update | Engine\Render\Resource\Buffer.cpp:308 |
| FAtlasQuadTreeBase::Init | Engine\Render\Shadow\AtlasQuadTreeBase.cpp:3 |
| FTileCullingVisualizer::Initialize | Engine\Render\Culling\TileBasedLightCulling.cpp:11 |
| FTileBasedLightCulling::Initialize | Engine\Render\Culling\TileBasedLightCulling.cpp:173 |
| FClusteredLightCuller::Initialize | Engine\Render\Culling\ClusteredLightCuller.cpp:22 |
| FGPUOcclusionCulling::Initialize | Engine\Render\Culling\GPUOcclusionCulling.cpp:30 |

### B10. 물리 / 파티클 / 프로파일링 / 기타
| 클래스::메서드 | 경로:라인 |
|---|---|
| FPhysXPhysicsScene::Initialize | Engine\Physics\PhysXPhysicsScene.cpp:401 |
| FPhysXPhysicsScene::Shutdown | Engine\Physics\PhysXPhysicsScene.cpp:439 |
| FPhysXPhysicsScene::Tick | Engine\Physics\PhysXPhysicsScene.cpp:586 |
| FNativePhysicsScene::Initialize | Engine\Physics\NativePhysicsScene.cpp:11 |
| FNativePhysicsScene::Shutdown | Engine\Physics\NativePhysicsScene.cpp:16 |
| FNativePhysicsScene::Tick | Engine\Physics\NativePhysicsScene.cpp:107 |
| FParticleEmitterInstance::Init | Engine\Particles\Runtime\ParticleEmitterInstance.cpp:17 |
| FParticleEmitterInstance::Tick | Engine\Particles\Runtime\ParticleEmitterInstance.cpp:27 |
| UParticleModuleCollision::Update | Engine\Particles\Module\ParticleModuleCollision.cpp:9 |
| FDebugDrawQueue::Tick | Engine\Debug\DebugDrawQueue.cpp:84 |
| FMaterialConstantBuffer::Init | Engine\Materials\MaterialCore.cpp:36 |
| FTimer::Initialize | Engine\Profiling\Time\Timer.cpp:3 |
| FTimer::Tick | Engine\Profiling\Time\Timer.cpp:8 |
| FGPUProfiler::Initialize | Engine\Profiling\GPUProfiler.cpp:7 |
| FGPUProfiler::Shutdown | Engine\Profiling\GPUProfiler.cpp:35 |

**B 소계: 144개 정의 (B1:15, B2:5, B3:4, B4:28, B5:13, B6:26, B7:15, B8:13, B9:10, B10:15).**

---

## 색인 C — ImGui 에디터 패널 / 노출 기능

### C1. `ImGui::Begin(` 패널 — 30개 (29 활성 + 1 주석)

| 창 제목 | 경로:라인 |
|---|---|
| "Mesh List" | ObjViewer\ObjViewerPanel.cpp:75 |
| "Preview" | ObjViewer\ObjViewerPanel.cpp:198 |
| "Viewport" | Editor\Viewport\Level\FLevelViewportLayout.cpp:880 |
| `OverlayID` (주석 처리됨) | Editor\Viewport\Level\FLevelViewportLayout.cpp:1229 |
| `WindowID` (동적, 통계 오버레이) | Editor\Subsystem\OverlayStatSystem.cpp:466 |
| "ContentBrowser" | Editor\UI\ContentBrowser\ContentBrowser.cpp:132 |
| `WindowTitle` (동적, 파티클 에디터) | Editor\UI\Asset\Particle\ParticleSystemEditorWidget.cpp:1495 |
| "ImGuiSetting" | Editor\UI\Util\ImGuiSetting.cpp:21 |
| `WindowTitle` (동적, StaticMesh 에디터) | Editor\UI\Asset\Mesh\StaticMeshEditorWidget.cpp:190 |
| `WindowTitle` (동적, Mesh 에디터) | Editor\UI\Asset\Mesh\MeshEditorWidget.cpp:369 |
| `WindowTitle` (동적, Material 에디터) | Editor\UI\Asset\Material\MaterialEditorWidget.cpp:188 |
| `WindowTitle` (동적, FloatCurve 에디터) | Editor\UI\Asset\Curve\FloatCurveEditorWidget.cpp:215 |
| `WindowTitle` (동적, AnimGraph 에디터) | Editor\UI\Asset\Animation\AnimGraphEditorWidget.cpp:606 |
| `WindowTitle` (동적, CameraShake 에디터) | Editor\UI\Asset\CameraShake\CameraShakeEditorWidget.cpp:403 |
| `Label` (동적, 메인 도크 호스트) | Editor\UI\EditorMainPanel.cpp:330 |
| "Shortcut Help" | Editor\UI\EditorMainPanel.cpp:347 |
| "Editor Debug" | Editor\UI\EditorMainPanel.cpp:377 |
| "##ConsoleDrawer" | Editor\UI\EditorMainPanel.cpp:609 |
| "##EditorFooter" | Editor\UI\EditorMainPanel.cpp:650 |
| "Shadow Map Debug" | Editor\UI\Debug\EditorShadowMapDebugWidget.cpp:261 |
| "Animation Debug" | Editor\UI\Debug\EditorAnimationDebugWidget.cpp:159 |
| "World Settings" | Editor\UI\Panel\EditorWorldSettingsWidget.cpp:14 |
| `WindowName` (동적, 뷰포트) | Editor\UI\Panel\EditorViewportWidget.cpp:26 |
| "Stat Profiler" | Editor\UI\Panel\EditorStatWidget.cpp:18 |
| "Scene Manager" | Editor\UI\Panel\EditorSceneWidget.cpp:23 |
| "Property Window" | Editor\UI\Panel\EditorPropertyWidget.cpp:256 |
| "Project Settings" | Editor\UI\Panel\EditorProjectSettingsWidget.cpp:13 |
| "MaterialInspector" | Editor\UI\Panel\EditorMaterialInspector.cpp:20 |
| "Control Panel" | Editor\UI\Panel\EditorControlWidget.cpp:19 |
| "Console" | Editor\UI\Panel\EditorConsoleWidget.cpp:302 |

### C2. `ImGui::MenuItem(` — 81개 (동작 트리거)

표기: (주석)=주석 처리, (표시)=disabled/표시 전용(클릭 동작 없음), (동적)=라벨이 변수.

EditorMainPanel.cpp
| 라벨 | 경로:라인 |
|---|---|
| "New Scene" (Ctrl+N) | Editor\UI\EditorMainPanel.cpp:227 |
| "Open Scene..." (Ctrl+O) | Editor\UI\EditorMainPanel.cpp:231 |
| "Save Scene" (Ctrl+S) | Editor\UI\EditorMainPanel.cpp:235 |
| "Save Scene As..." (Ctrl+Shift+S) | Editor\UI\EditorMainPanel.cpp:239 |
| `CurrentSceneLabel` (표시) | Editor\UI\EditorMainPanel.cpp:255 |
| "Windows" | Editor\UI\EditorMainPanel.cpp:260 |
| "Project Settings" | Editor\UI\EditorMainPanel.cpp:283 |
| "World Settings" | Editor\UI\EditorMainPanel.cpp:288 |
| "Shortcut" | Editor\UI\EditorMainPanel.cpp:293 |

ContentBrowser.cpp / ContentBrowserElement.cpp
| 라벨 | 경로:라인 |
|---|---|
| "Float Curve" | Editor\UI\ContentBrowser\ContentBrowser.cpp:552 |
| "Camera Shake" | Editor\UI\ContentBrowser\ContentBrowser.cpp:567 |
| "Anim Graph" | Editor\UI\ContentBrowser\ContentBrowser.cpp:582 |
| "Particle System" | Editor\UI\ContentBrowser\ContentBrowser.cpp:597 |
| "Refresh" | Editor\UI\ContentBrowser\ContentBrowser.cpp:616 |
| "Rename" | Editor\UI\ContentBrowser\ContentBrowserElement.cpp:411 |
| "Reimport" | Editor\UI\ContentBrowser\ContentBrowserElement.cpp:564 |
| "Open Imported Asset" | Editor\UI\ContentBrowser\ContentBrowserElement.cpp:658 |
| "Reimport Options..." / "Import Options..." (동적) | Editor\UI\ContentBrowser\ContentBrowserElement.cpp:663 |
| "Reimport" | Editor\UI\ContentBrowser\ContentBrowserElement.cpp:678 |

FLevelViewportLayout.cpp
| 라벨 | 경로:라인 |
|---|---|
| `Label` (동적) | Editor\Viewport\Level\FLevelViewportLayout.cpp:1707 |
| `Entry.Label` (동적) | Editor\Viewport\Level\FLevelViewportLayout.cpp:1745 |
| "Delete" (주석) | Editor\Viewport\Level\FLevelViewportLayout.cpp:1766 |

AnimationTimelinePanel.cpp / AnimGraphEditorWidget.cpp
| 라벨 | 경로:라인 |
|---|---|
| `Cls->GetName()` (동적) | Editor\UI\Asset\Animation\AnimationTimelinePanel.cpp:739 |
| `Cls->GetName()` (동적) | Editor\UI\Asset\Animation\AnimationTimelinePanel.cpp:763 |
| "Rename" | Editor\UI\Asset\Animation\AnimationTimelinePanel.cpp:981 |
| "Delete" | Editor\UI\Asset\Animation\AnimationTimelinePanel.cpp:985 |
| "Delete Key" | Editor\UI\Asset\Animation\AnimationTimelinePanel.cpp:1287 |
| "Delete" | Editor\UI\Asset\Animation\AnimGraphEditorWidget.cpp:797 |
| "Delete" | Editor\UI\Asset\Animation\AnimGraphEditorWidget.cpp:806 |
| `NodeTypeLabel(Type)` (동적) | Editor\UI\Asset\Animation\AnimGraphEditorWidget.cpp:829 |

ParticleSystemEditorWidget.cpp — 45개
| 라벨 | 경로:라인 |
|---|---|
| "Delete Module" | ...\ParticleSystemEditorWidget.cpp:791 |
| "Refresh Module" | ...\ParticleSystemEditorWidget.cpp:797 |
| "Duplicate From Higher" | ...\ParticleSystemEditorWidget.cpp:803 |
| "Share From Higher" | ...\ParticleSystemEditorWidget.cpp:807 |
| "Duplicate From Highest" | ...\ParticleSystemEditorWidget.cpp:814 |
| "Pause"/"Play" (동적) | ...\ParticleSystemEditorWidget.cpp:2216 |
| "No modules available" (표시) | ...\ParticleSystemEditorWidget.cpp:2866 |
| "Rename Emitter" (표시) | ...\ParticleSystemEditorWidget.cpp:2880 |
| "Duplicate Emitter" | ...\ParticleSystemEditorWidget.cpp:2883 |
| "Duplicate and Share Emitter" (표시) | ...\ParticleSystemEditorWidget.cpp:2889 |
| "Delete Emitter" | ...\ParticleSystemEditorWidget.cpp:2892 |
| "Export Emitter" (표시) | ...\ParticleSystemEditorWidget.cpp:2898 |
| "Export All" (표시) | ...\ParticleSystemEditorWidget.cpp:2899 |
| "Select Particle System" | ...\ParticleSystemEditorWidget.cpp:2910 |
| "Add New Emitter Before" | ...\ParticleSystemEditorWidget.cpp:2914 |
| "Add New Emitter After" | ...\ParticleSystemEditorWidget.cpp:2918 |
| "Remove Duplicate Modules" (표시) | ...\ParticleSystemEditorWidget.cpp:2924 |
| "Sprite" | ...\ParticleSystemEditorWidget.cpp:2936 |
| "Mesh" | ...\ParticleSystemEditorWidget.cpp:2940 |
| "Ribbon" | ...\ParticleSystemEditorWidget.cpp:2944 |
| "Beam" | ...\ParticleSystemEditorWidget.cpp:2948 |
| "Acceleration" | ...\ParticleSystemEditorWidget.cpp:2957 |
| "Beam Source" | ...\ParticleSystemEditorWidget.cpp:2968 |
| "Beam Noise" | ...\ParticleSystemEditorWidget.cpp:2972 |
| "Beam Target" | ...\ParticleSystemEditorWidget.cpp:2976 |
| "Collision" | ...\ParticleSystemEditorWidget.cpp:2985 |
| "Color Over Life" | ...\ParticleSystemEditorWidget.cpp:2994 |
| "Event Generator" | ...\ParticleSystemEditorWidget.cpp:3003 |
| "Event Receiver" | ...\ParticleSystemEditorWidget.cpp:3007 |
| "Lifetime" | ...\ParticleSystemEditorWidget.cpp:3016 |
| "Initial Location" | ...\ParticleSystemEditorWidget.cpp:3025 |
| "Initial Rotation" | ...\ParticleSystemEditorWidget.cpp:3034 |
| "Initial Rotation Rate" | ...\ParticleSystemEditorWidget.cpp:3042 |
| "Initial Size" | ...\ParticleSystemEditorWidget.cpp:3053 |
| "Sub Image Index" | ...\ParticleSystemEditorWidget.cpp:3064 |
| "Initial Velocity" | ...\ParticleSystemEditorWidget.cpp:3075 |
| "Set Time" | ...\ParticleSystemEditorWidget.cpp:4676 |
| "Set Value" | ...\ParticleSystemEditorWidget.cpp:4682 |
| "Delete Key" | ...\ParticleSystemEditorWidget.cpp:4689 |
| "Constant" (동적 체크) | ...\ParticleSystemEditorWidget.cpp:4710 |
| "Linear" (동적 체크) | ...\ParticleSystemEditorWidget.cpp:4721 |
| "Cubic" (동적 체크) | ...\ParticleSystemEditorWidget.cpp:4732 |
| "Add Key" | ...\ParticleSystemEditorWidget.cpp:4808 |
| "Fit To Keys" | ...\ParticleSystemEditorWidget.cpp:4835 |
| "Remove Curve" | ...\ParticleSystemEditorWidget.cpp:4905 |

FloatCurveEditorWidget.cpp — 6개
| 라벨 | 경로:라인 |
|---|---|
| "Delete Key" | Editor\UI\Asset\Curve\FloatCurveEditorWidget.cpp:579 |
| "Constant" (동적 체크) | Editor\UI\Asset\Curve\FloatCurveEditorWidget.cpp:589 |
| "Linear" (동적 체크) | Editor\UI\Asset\Curve\FloatCurveEditorWidget.cpp:594 |
| "Cubic" (동적 체크) | Editor\UI\Asset\Curve\FloatCurveEditorWidget.cpp:599 |
| "Add Key" | Editor\UI\Asset\Curve\FloatCurveEditorWidget.cpp:613 |
| "Fit To Keys" | Editor\UI\Asset\Curve\FloatCurveEditorWidget.cpp:632 |

### C3. `ImGui::Button(` — 63개 (동작 트리거)

표기: (주석)=주석 처리, (표시)=placeholder/비액션, (동적)=라벨이 변수(아이콘 버튼 폴백 등).

| 라벨 | 경로:라인 |
|---|---|
| "Import..." | ObjViewer\ObjViewerPanel.cpp:94 |
| "Import" | ObjViewer\ObjViewerPanel.cpp:173 |
| "Cancel" | ObjViewer\ObjViewerPanel.cpp:186 |
| `Label` (주석) | Editor\Viewport\Level\FLevelViewportLayout.cpp:1271 |
| `ToggleLabel` (주석) | Editor\Viewport\Level\FLevelViewportLayout.cpp:1312 |
| `CurrentTypeName` (주석) | Editor\Viewport\Level\FLevelViewportLayout.cpp:1338 |
| "Reset Camera" (주석) | Editor\Viewport\Level\FLevelViewportLayout.cpp:1521 |
| "View Light" (주석) | Editor\Viewport\Level\FLevelViewportLayout.cpp:1562 |
| "Save Setting" | Editor\UI\Util\ImGuiSetting.cpp:30 |
| "Spawn Grid Actors" | Editor\UI\EditorMainPanel.cpp:458 |
| "Clear Last Batch" | Editor\UI\EditorMainPanel.cpp:543 |
| `FallbackLabel` (동적, 툴바 아이콘) | Editor\UI\Toolbar\ViewportToolbar.cpp:78 |
| `FallbackLabel` (동적, 툴바 아이콘) | Editor\UI\Toolbar\ViewportToolbar.cpp:283 |
| `ViewportTypeNames[..]` (동적) | Editor\UI\Toolbar\ViewportToolbar.cpp:379 |
| "Import" | Editor\UI\Dialog\FbxImportOptionsDialog.cpp:259 |
| "Cancel" | Editor\UI\Dialog\FbxImportOptionsDialog.cpp:306 |
| "Import" | Editor\UI\Dialog\FbxImportOptionsDialog.cpp:393 |
| "Cancel" | Editor\UI\Dialog\FbxImportOptionsDialog.cpp:420 |
| `ButtonLabel` (동적) | Editor\UI\Panel\EditorViewportWidget.cpp:38 |
| "Resume" | Editor\UI\Panel\EditorStatWidget.cpp:23 |
| "Copy" | Editor\UI\Panel\EditorStatWidget.cpp:28 |
| "Pause" | Editor\UI\Panel\EditorStatWidget.cpp:68 |
| `RemoveLabel` (동적) | Editor\UI\Panel\EditorPropertyWidget.cpp:302 |
| "Rename" (주석) | Editor\UI\Panel\EditorPropertyWidget.cpp:338 |
| "Add" | Editor\UI\Panel\EditorPropertyWidget.cpp:567 |
| "Remove" | Editor\UI\Panel\EditorPropertyWidget.cpp:796 |
| "Edit Script" | Editor\UI\Panel\EditorPropertyRenderer.cpp:573 |
| "Import FBX" | Editor\UI\Panel\EditorPropertyRenderer.cpp:647 |
| "Edit Script" | Editor\UI\Panel\EditorPropertyRenderer.cpp:809 |
| "Import" | Editor\UI\Panel\EditorPropertyRenderer.cpp:923 |
| "Import" | Editor\UI\Panel\EditorPropertyRenderer.cpp:955 |
| "Cancel" | Editor\UI\Panel\EditorPropertyRenderer.cpp:976 |
| "-" | Editor\UI\Panel\EditorPropertyRenderer.cpp:1136 |
| "None" (표시) | Editor\UI\Panel\EditorPropertyRenderer.cpp:1245 |
| "Import" | Editor\UI\Panel\EditorPropertyRenderer.cpp:1496 |
| "Import FBX" | Editor\UI\Panel\EditorPropertyRenderer.cpp:1570 |
| "OK" | Editor\UI\ContentBrowser\ContentBrowser.cpp:186 |
| "Cancel" | Editor\UI\ContentBrowser\ContentBrowser.cpp:188 |
| `FallbackLabel` (동적, 재생 툴바) | Editor\UI\Panel\EditorPlayToolbarWidget.cpp:58 |
| "OK" | Editor\UI\Asset\Animation\AnimationTimelinePanel.cpp:1007 |
| `Fallback` (동적, 트랜스포트바) | Editor\UI\Asset\Animation\AnimationTransportBar.cpp:43 |
| "Add State" | Editor\UI\Asset\Animation\AnimGraphEditorWidget.cpp:431 |
| "Delete##State" | Editor\UI\Asset\Animation\AnimGraphEditorWidget.cpp:472 |
| "Add Transition" | Editor\UI\Asset\Animation\AnimGraphEditorWidget.cpp:505 |
| "Delete##Trans" | Editor\UI\Asset\Animation\AnimGraphEditorWidget.cpp:517 |
| "Save" | Editor\UI\Asset\Animation\AnimGraphEditorWidget.cpp:617 |
| "##skelSplitter" (표시, 스플리터) | Editor\UI\Asset\Mesh\MeshEditorWidget.cpp:606 |
| "Load..." | Editor\UI\Asset\Mesh\MeshEditorWidget.cpp:1043 |
| "Import Animation FBX" | Editor\UI\Asset\Mesh\MeshEditorWidget.cpp:1065 |
| "+ New Morph Animation" | Editor\UI\Asset\Mesh\MeshEditorWidget.cpp:1078 |
| "+ New Montage (from selected sequence)" | Editor\UI\Asset\Mesh\MeshEditorWidget.cpp:1170 |
| "+ Add Section" | Editor\UI\Asset\Animation\AnimMontagePropertyPanel.cpp:171 |
| "Play##montagePlay" | Editor\UI\Asset\Animation\AnimMontagePropertyPanel.cpp:224 |
| "Stop##montageStop" | Editor\UI\Asset\Animation\AnimMontagePropertyPanel.cpp:231 |
| "Create Instance" | Editor\UI\Asset\Material\MaterialEditorWidget.cpp:276 |
| "Open Parent" | Editor\UI\Asset\Material\MaterialEditorWidget.cpp:288 |
| "None" (표시) | Editor\UI\Asset\Material\MaterialEditorWidget.cpp:590 |
| "Clear" | Editor\UI\Asset\Material\MaterialEditorWidget.cpp:640 |
| "Save" | Editor\UI\Asset\Curve\FloatCurveEditorWidget.cpp:225 |
| "Fit To Keys" | Editor\UI\Asset\Curve\FloatCurveEditorWidget.cpp:666 |
| "Save" | Editor\UI\Asset\CameraShake\CameraShakeEditorWidget.cpp:413 |
| "Stop Preview"/"Play Preview" (동적) | Editor\UI\Asset\CameraShake\CameraShakeEditorWidget.cpp:455 |
| "Reset" | Editor\UI\Asset\CameraShake\CameraShakeEditorWidget.cpp:464 |

**C 소계: 174건 (Begin 30 + MenuItem 81 + Button 63).**
순수 동작 트리거(주석·표시전용 제외) 기준 대략: Begin 29, MenuItem ~73, Button ~50.

---

## 자기점검 (self-check)

### 1. 수집 규모
- **색인 A: 매니저/서브시스템 클래스 N = 77개** (A1 39 + A2 38). 전부 동명 `.cpp` 존재.
- **색인 B: 생명주기 훅 정의 M = 144개** (원시 173건 − 호출부/주석 29건).
- **색인 C: 노출 진입점 K = 174건** (Begin 30 + MenuItem 81 + Button 63).
- **총 진입점 N + M + K = 77 + 144 + 174 = 395개.**

### 2. 다음 단계 선언
다음 단계에서 위 **395개 진입점을 빠짐없이** 각 기능으로 전개한다. (단, C의 (주석)·(표시전용) 항목은 "비활성/미구현"으로 분류만 하고 기능에서 제외 판정한다.)

### 3. 본 색인이 놓쳤을 가능성이 있는 영역 (정의된 grep 규칙의 한계)

- **B 규칙이 놓친 게임플레이/컴포넌트 생명주기 훅 (≈90건 확인).**
  엄격한 이름 목록(Init/Initialize/Startup/Update/Tick/Render/Draw/Shutdown/Destroy)은 Unreal식 핵심 훅을 통째로 누락한다. `Class::(BeginPlay|EndPlay|TickComponent|TickActor|...)` 정의/호출이 **33개 파일에 90건** 존재(AActor, ACharacter, GameModeBase, PlayerController, 다수 *Component·*Actor). 특히 **`BeginPlay`/`EndPlay`/`TickComponent`** 는 컴포넌트·액터의 실제 진입점이므로 다음 단계에서 별도 색인(B+)으로 반드시 보강해야 함.

- **A 접미사 규칙이 놓친 클래스 군 (≈145건 매칭 확인).**
  `*ViewportClient / *Widget / *Pipeline / *Proxy / *Instance / *Pass / *Factory / *Application / *Window` 로 끝나는 클래스가 **116개 파일에 145건**. 누락된 대형 서브시스템 계열:
  - **렌더 파이프라인**: `IRenderPipeline`, `DefaultRenderPipeline`, `GameRenderPipeline`, `EditorRenderPipeline`, `ObjViewerRenderPipeline` (`*Pipeline` 접미사 미포함).
  - **렌더 패스 시스템**: `OpaquePass / PreDepthPass / ShadowMapPass / FogPass / DecalPass / BloomPass / FXAAPass / GammaCorrectionPass / PostProcessPass / UIPass / LightCullingPass / SelectionMaskPass / GizmoInnerPass / GizmoOuterPass / EditorLinesPass / OverlayFontPass / AdditiveDecalPass / AlphaBlendPass` (≈18개).
  - **씬 프록시**: `*SceneProxy` 다수(StaticMesh, SkeletalMesh, Billboard, Decal, Particle, Gizmo, Shape, SubUV, TextRender, BoneDebug …).
  - **에디터 패널 클래스 본체**: `FEditorSceneWidget`, `FEditorConsoleWidget`, `FEditorPropertyWidget` 등 `*Widget` 류(색인 C의 Begin/메서드로는 잡히나, A의 "클래스" 목록에선 누락).
  - **뷰포트 클라이언트**: `FEditorViewportClient`, `FLevelEditorViewportClient`, `FGameViewportClient`, `FEditorPreviewViewportClient`, 에셋별 *ViewportClient.
  - **애님 인스턴스**: `UAnimInstance`, `UAnimSingleNodeInstance`, `UCharacterAnimInstance`, `ULuaAnimInstance`, `UCharacterAnimGraphInstance`, `UAnimGraphInstance`.
  - **Slate UI**: `SWidget`, `SWindow`, `SSplitter`, `SlateApplication` (S 접두 위젯 체계).
  - **팩토리/플랫폼**: `AssetFactory`, `ObjectFactory`, `FWindowsApplication`, `FWindowsWindow`.
  - **베이스 클래스**: `ULightComponentBase`, `FRenderPassBase` 등 `*Base` 접미사는 A 규칙에서 누락.

- **A는 `class` 만 검색.** `struct` 로 선언된 매니저/서비스류가 있으면 누락.

- **B는 `.cpp` out-of-line 정의만.** 헤더 인라인 정의/순수가상 선언(`virtual ... = 0`)·인터페이스 훅은 미포함.

- **C의 동적 라벨.** Begin/MenuItem/Button 중 다수가 변수 라벨(`WindowTitle`, `FallbackLabel`, `Cls->GetName()`, `ViewportTypeNames[..]`)이라 grep으로 텍스트가 안 드러남 → 실제 창/메뉴 제목은 코드 추적 필요. 또한 원시 카운트에는 주석 처리된 호출이 포함되어 있어 활성 항목과 구분해 표기함.

- **ImGui 밖 UI 표면.** 본 색인은 ImGui 에디터 UI에 한정. 런타임 게임 UI는 **RmlUi 기반**(`UUIManager`/`UUserWidget`, ThirdParty\RmlUi)으로 별도 surface이며 `ImGui::Begin` 색인에 안 잡힘.

- **스크립트/콘텐츠 진입점.** **Lua(sol2/lua)** 바인딩(`FLuaScriptManager`, `Game\Lua\GameLuaBindings.cpp`, `ULuaScriptComponent`)은 C++ 클래스 색인과 별개로 게임 로직 진입점이 될 수 있음.

- **README 부재.** 리포 루트·서브에 README가 없어 디렉터리 구조만으로 영역을 추정함. 위 누락 계열은 디렉터리(`Render/RenderPass`, `Render/Proxy`, `Editor/Slate`, `Animation/Instance`, `Render/Pipeline`)로 교차 확인한 결과임.

---
---

# 보강 색인 (2차) — 자기점검 3번 신고 영역만

> 1차 395개는 재수집하지 않음. 아래는 **신규 항목만**. 규칙 동일: grep 근거·추측 금지·경로:라인 필수.

## 색인 B+ — 게임플레이/컴포넌트 생명주기 훅 (자기점검 3-1)

규칙: `.cpp`의 `Class::(BeginPlay|EndPlay|TickComponent|TickActor|PostInit*|OnRegister|OnUnregister|InitializeComponent|UninitializeComponent|Possess|UnPossess|SetupPlayerInputComponent|OnConstruction)(` out-of-line 정의.
원시 매칭 92줄에서 호출부(`Super::`/`UParent::`)·주석 40줄 제외 → **정의 52건**.
매칭 0(정의 없음): `PostInit*`, `OnRegister`, `OnUnregister`, `InitializeComponent`, `UninitializeComponent`, `SetupPlayerInputComponent`, `OnConstruction`.

### B+1. Engine / World / Level / Actor 베이스
| 클래스::메서드 | 경로:라인 |
|---|---|
| UEngine::BeginPlay | Engine\Runtime\Engine.cpp:108 |
| UWorld::BeginPlay | Engine\GameFramework\World.cpp:315 |
| UWorld::EndPlay | Engine\GameFramework\World.cpp:389 |
| ULevel::BeginPlay | Engine\GameFramework\Level.cpp:85 |
| ULevel::EndPlay | Engine\GameFramework\Level.cpp:98 |
| AActor::BeginPlay | Engine\GameFramework\AActor.cpp:183 |
| AActor::TickActor | Engine\GameFramework\AActor.cpp:208 |
| AActor::EndPlay | Engine\GameFramework\AActor.cpp:217 |

### B+2. Actor / Pawn / Controller / GameMode
| 클래스::메서드 | 경로:라인 |
|---|---|
| AStaticMeshActor::BeginPlay | Engine\GameFramework\Actor\StaticMeshActor.cpp:11 |
| ASkeletalMeshActor::BeginPlay | Engine\GameFramework\Actor\SkeletalMeshActor.cpp:8 |
| ASphereActor::BeginPlay | Engine\GameFramework\Actor\SphereActor.cpp:19 |
| ATriggerVolumeBase::BeginPlay | Engine\GameFramework\Actor\TriggerVolumeBase.cpp:28 |
| APawn::BeginPlay | Engine\GameFramework\Pawn\Pawn.cpp:11 |
| APlayerController::BeginPlay | Engine\GameFramework\GameMode\PlayerController.cpp:8 |
| APlayerController::Possess | Engine\GameFramework\GameMode\PlayerController.cpp:56 |
| APlayerController::UnPossess | Engine\GameFramework\GameMode\PlayerController.cpp:69 |
| AGameModeBase::BeginPlay | Engine\GameFramework\GameMode\GameModeBase.cpp:17 |
| AGameModeBase::EndPlay | Engine\GameFramework\GameMode\GameModeBase.cpp:30 |

### B+3. Component — BeginPlay / EndPlay
| 클래스::메서드 | 경로:라인 |
|---|---|
| UActorComponent::BeginPlay | Engine\Component\ActorComponent.cpp:8 |
| UPrimitiveComponent::BeginPlay | Engine\Component\PrimitiveComponent.cpp:51 |
| UPrimitiveComponent::EndPlay | Engine\Component\PrimitiveComponent.cpp:73 |
| UCameraComponent::BeginPlay | Engine\Component\Camera\CameraComponent.cpp:10 |
| UCameraComponent::EndPlay | Engine\Component\Camera\CameraComponent.cpp:28 |
| USpringArmComponent::BeginPlay | Engine\Component\Camera\SpringArmComponent.cpp:12 |
| UActionComponent::BeginPlay | Engine\Component\Input\ActionComponent.cpp:26 |
| UActionComponent::EndPlay | Engine\Component\Input\ActionComponent.cpp:31 |
| ULuaScriptComponent::BeginPlay | Engine\Component\Script\LuaScriptComponent.cpp:80 |
| ULuaScriptComponent::EndPlay | Engine\Component\Script\LuaScriptComponent.cpp:101 |
| UParticleSystemComponent::EndPlay | Engine\Component\Particle\ParticleSystemComponent.cpp:129 |
| UMovementComponent::BeginPlay | Engine\Component\Movement\MovementComponent.cpp:30 |
| UProjectileMovementComponent::BeginPlay | Engine\Component\Movement\ProjectileMovementComponent.cpp:46 |
| UFloatingPawnMovementComponent::BeginPlay | Engine\Component\Movement\FloatingPawnMovementComponent.cpp:36 |
| UPendulumMovementComponent::BeginPlay | Engine\Component\Movement\PendulumMovementComponent.cpp:11 |

### B+4. Component — TickComponent
| 클래스::메서드 | 경로:라인 |
|---|---|
| UActorComponent::TickComponent | Engine\Component\ActorComponent.cpp:34 |
| UInputComponent::TickComponent | Engine\Component\Input\InputComponent.cpp:46 |
| UActionComponent::TickComponent | Engine\Component\Input\ActionComponent.cpp:37 |
| ULuaScriptComponent::TickComponent | Engine\Component\Script\LuaScriptComponent.cpp:315 |
| USpringArmComponent::TickComponent | Engine\Component\Camera\SpringArmComponent.cpp:18 |
| UTemporaryBoneAnimatorComponent::TickComponent | Engine\Component\Debug\TemporaryBoneAnimatorComponent.cpp:27 |
| UDecalComponent::TickComponent | Engine\Component\Primitive\DecalComponent.cpp:17 |
| UBillboardComponent::TickComponent | Engine\Component\Primitive\BillboardComponent.cpp:70 |
| UCylindricalBillboardComponent::TickComponent | Engine\Component\Primitive\CylindricalBillboardComponent.cpp:15 |
| USubUVComponent::TickComponent | Engine\Component\Primitive\SubUVComponent.cpp:109 |
| USkinnedMeshComponent::TickComponent | Engine\Component\Primitive\SkinnedMeshComponent.cpp:1311 |
| USkeletalMeshComponent::TickComponent | Engine\Component\Primitive\SkeletalMeshComponent.cpp:293 |
| UParticleSystemComponent::TickComponent | Engine\Component\Particle\ParticleSystemComponent.cpp:174 |
| UMovementComponent::TickComponent | Engine\Component\Movement\MovementComponent.cpp:36 |
| URotatingMovementComponent::TickComponent | Engine\Component\Movement\RotatingMovementComponent.cpp:9 |
| UProjectileMovementComponent::TickComponent | Engine\Component\Movement\ProjectileMovementComponent.cpp:51 |
| UFloatingPawnMovementComponent::TickComponent | Engine\Component\Movement\FloatingPawnMovementComponent.cpp:42 |
| UPendulumMovementComponent::TickComponent | Engine\Component\Movement\PendulumMovementComponent.cpp:22 |
| UCharacterMovementComponent::TickComponent | Engine\Component\Movement\CharacterMovementComponent.cpp:86 |

**B+ 소계: 52건 / 32개 클래스.**
B(기존)와 **중복 클래스 4개**: `UEngine`, `UWorld`, `ULevel`, `AActor` (B에는 Init/Tick/Render만, B+에서 BeginPlay/EndPlay/TickActor 추가).
나머지 **28개 클래스는 인벤토리 신규** (컴포넌트·액터·Pawn·Controller·GameMode 군 — B의 이름 규칙이 통째로 놓쳤던 것).

---

## 색인 A+ — 렌더/애님 미포함 클래스군 (자기점검 3-2)

규칙: `class`/`struct` 선언 중 이름이 `*Pipeline|*Pass|*SceneProxy|*ViewportClient|*AnimInstance|*Factory|*Base` 로 끝나는 것. 전방선언·friend·enum·멤버참조 제외. (A의 *Manager/*System과 접미사 비중복.)

### A+1. *ViewportClient — 9개
| 클래스 | 헤더:라인 | .cpp |
|---|---|---|
| FViewportClient | Engine\Viewport\ViewportClient.h:8 | ✓ |
| FObjViewerViewportClient | ObjViewer\ObjViewerViewportClient.h:11 | ✓ |
| UGameViewportClient | Engine\Viewport\GameViewportClient.h:20 | ✓ |
| FEditorViewportClient | Editor\Viewport\EditorViewportClient.h:26 | ✓ |
| FLevelEditorViewportClient | Editor\Viewport\Level\LevelEditorViewportClient.h:10 | ✗ 헤더전용 |
| IEditorPreviewViewportClient | Editor\Viewport\EditorPreviewViewportClient.h:9 | ✗ 인터페이스 |
| FStaticMeshEditorViewportClient | Editor\Viewport\Asset\StaticMeshEditorViewportClient.h:15 | ✓ |
| FMeshEditorViewportClient | Editor\Viewport\Asset\MeshEditorViewportClient.h:20 | ✓ |
| FParticleSystemEditorViewportClient | Editor\Viewport\Asset\ParticleSystemEditorViewportClient.h:15 | ✓ |

### A+2. *Pipeline — 6개 (m = 6)
| 클래스 | 헤더:라인 | .cpp |
|---|---|---|
| IRenderPipeline | Engine\Render\Pipeline\IRenderPipeline.h:5 | ✗ 인터페이스 |
| FDefaultRenderPipeline | Engine\Render\Pipeline\DefaultRenderPipeline.h:8 | ✓ |
| FGameRenderPipeline | Engine\Runtime\GameRenderPipeline.h:9 | ✓ |
| FEditorRenderPipeline | Editor\EditorRenderPipeline.h:15 | ✓ |
| FObjViewerRenderPipeline | ObjViewer\ObjViewerRenderPipeline.h:11 | ✓ |
| FRenderPassPipeline | Engine\Render\RenderPass\RenderPassPipeline.h:13 | ✓ |

### A+3. *Pass — 18개 (n = 18, 전부 `: public FRenderPassBase`, 전부 .cpp ✓)
| 클래스 | 헤더:라인 |
|---|---|
| FOpaquePass | Engine\Render\RenderPass\OpaquePass.h:5 |
| FPreDepthPass | Engine\Render\RenderPass\PreDepthPass.h:5 |
| FShadowMapPass | Engine\Render\RenderPass\ShadowMapPass.h:31 |
| FLightCullingPass | Engine\Render\RenderPass\LightCullingPass.h:5 |
| FDecalPass | Engine\Render\RenderPass\DecalPass.h:5 |
| FAdditiveDecalPass | Engine\Render\RenderPass\AdditiveDecalPass.h:5 |
| FAlphaBlendPass | Engine\Render\RenderPass\AlphaBlendPass.h:5 |
| FFogPass | Engine\Render\RenderPass\FogPass.h:5 |
| FBloomPass | Engine\Render\RenderPass\BloomPass.h:6 |
| FFXAAPass | Engine\Render\RenderPass\FXAAPass.h:5 |
| FGammaCorrectionPass | Engine\Render\RenderPass\GammaCorrectionPass.h:5 |
| FPostProcessPass | Engine\Render\RenderPass\PostProcessPass.h:5 |
| FUIPass | Engine\Render\RenderPass\UIPass.h:5 |
| FSelectionMaskPass | Engine\Render\RenderPass\SelectionMaskPass.h:5 |
| FGizmoInnerPass | Engine\Render\RenderPass\GizmoInnerPass.h:5 |
| FGizmoOuterPass | Engine\Render\RenderPass\GizmoOuterPass.h:5 |
| FEditorLinesPass | Engine\Render\RenderPass\EditorLinesPass.h:5 |
| FOverlayFontPass | Engine\Render\RenderPass\OverlayFontPass.h:5 |

### A+4. *SceneProxy — 12개 (전부 .cpp ✓)
| 클래스 | 헤더:라인 |
|---|---|
| FPrimitiveSceneProxy | Engine\Render\Proxy\PrimitiveSceneProxy.h:48 |
| FStaticMeshSceneProxy | Engine\Render\Proxy\StaticMeshSceneProxy.h:13 |
| FSkeletalMeshSceneProxy | Engine\Render\Proxy\SkeletalMeshSceneProxy.h:8 |
| FBillboardSceneProxy | Engine\Render\Proxy\BillboardSceneProxy.h:12 |
| FCylindricalBillboardSceneProxy | Engine\Render\Proxy\CylindricalBillboardSceneProxy.h:7 |
| FSubUVSceneProxy | Engine\Render\Proxy\SubUVSceneProxy.h:14 |
| FTextRenderSceneProxy | Engine\Render\Proxy\TextRenderSceneProxy.h:14 |
| FDecalSceneProxy | Engine\Render\Proxy\DecalSceneProxy.h:13 |
| FParticleSystemSceneProxy | Engine\Render\Proxy\ParticleSystemSceneProxy.h:34 |
| FGizmoSceneProxy | Engine\Render\Proxy\GizmoSceneProxy.h:14 |
| FShapeSceneProxy | Engine\Render\Proxy\ShapeSceneProxy.h:21 |
| FBoneDebugSceneProxy | Engine\Render\Proxy\BoneDebugSceneProxy.h:7 |

### A+5. *AnimInstance — 3개 (전부 .cpp ✓)
| 클래스 | 헤더:라인 |
|---|---|
| UAnimInstance | Engine\Animation\AnimInstance.h:58 |
| UCharacterAnimInstance | Engine\Animation\Instance\CharacterAnimInstance.h:17 |
| ULuaAnimInstance | Engine\Animation\Instance\LuaAnimInstance.h:31 |

### A+6. *Factory — 3개
| 클래스 | 헤더:라인 | .cpp |
|---|---|---|
| FAssetFactory | Editor\Subsystem\AssetFactory.h:5 | ✓ |
| FObjectFactory | Engine\Object\Reflection\ObjectFactory.h:19 | ✗ 헤더전용(TSingleton) |
| FDelegateHandleFactory | Engine\Core\Delegate.h:28 | ✗ 헤더전용 |

### A+7. *Base — 14개 (struct 3 포함)
| 클래스 | 헤더:라인 | .cpp |
|---|---|---|
| UAnimSequenceBase | Engine\Animation\Sequence\AnimSequenceBase.h:15 | ✓ |
| ULightComponentBase | Engine\Component\Light\LightComponentBase.h:25 | ✓ |
| UCameraShakeBase | Engine\GameFramework\Camera\CameraShakeBase.h:23 | ✓ |
| AGameModeBase | Engine\GameFramework\GameMode\GameModeBase.h:20 | ✓ (B+ 중복) |
| AGameStateBase | Engine\GameFramework\GameMode\GameStateBase.h:14 | ✓ |
| ATriggerVolumeBase | Engine\GameFramework\Actor\TriggerVolumeBase.h:26 | ✓ (B+ 중복) |
| UParticleModuleTypeDataBase | Engine\Particles\Module\ParticleModuleTypeDataBase.h:126 | ✗ 헤더전용 |
| FRenderPassBase | Engine\Render\RenderPass\RenderPassBase.h:38 | ✓ |
| FAtlasQuadTreeBase | Engine\Render\Shadow\AtlasQuadTreeBase.h:21 | ✓ |
| FAnimNode_Base | Engine\Animation\Nodes\AnimNode_Base.h:26 | ✗ 헤더전용 |
| FObjectPropertyBase (struct) | Engine\Core\Types\PropertyTypes.h:260 | ✓ (PropertyTypes.cpp) |
| FDynamicEmitterReplayDataBase (struct) | Engine\Render\Proxy\ParticleDynamicEmitterData.h:15 | ✓ |
| FDynamicEmitterDataBase (struct) | Engine\Render\Proxy\ParticleDynamicEmitterData.h:22 | ✓ |
| FDynamicSpriteEmitterDataBase (struct) | Engine\Render\Proxy\ParticleDynamicEmitterData.h:36 | ✓ |

**A+ 소계: 65개** (ViewportClient 9 + Pipeline 6 + Pass 18 + SceneProxy 12 + AnimInstance 3 + Factory 3 + Base 14).
`.cpp` 존재 58 / 헤더전용 7 (`FLevelEditorViewportClient`, `IEditorPreviewViewportClient`, `IRenderPipeline`, `FObjectFactory`, `FDelegateHandleFactory`, `FAnimNode_Base`, `UParticleModuleTypeDataBase`).

---

## 색인 A++ — struct 매니저/서비스 (자기점검 3 끝줄)

규칙: `struct` 선언 중 `*System|*Manager|*Subsystem|*Service|*Pipeline|*Pass`.
**결과: 0건.** 1차 A의 매니저/서브시스템·및 A+의 Pipeline/Pass는 전부 `class` 선언이며, struct로 선언된 매니저/서비스류는 존재하지 않음.

---

## 색인 C+ — 동적 ImGui 창 제목 추적 (자기점검 3 C항, 선별)

C1에서 `(동적)`으로 표기했던 항목 중 대상 8건의 실제 제목을 코드 추적. 버튼/메뉴 아이콘 폴백 라벨은 추적 제외(지시대로).

| C1 동적 항목 | Begin 경로:라인 | 추적된 실제 제목 | 근거(대입 위치) |
|---|---|---|---|
| StaticMesh 에디터 | Editor\UI\Asset\Mesh\StaticMeshEditorWidget.cpp:190 | `Static Mesh Editor` + `###StaticMeshEditor_<Id>` | VisibleTitle :165 / WindowIdSuffix :38 |
| Mesh 에디터 | Editor\UI\Asset\Mesh\MeshEditorWidget.cpp:369 | `Mesh Editor` + `###MeshEditor_<Id>` | :344 / :180 |
| Material 에디터 | Editor\UI\Asset\Material\MaterialEditorWidget.cpp:188 | `Material Editor` + `###MaterialEditor_<Id>` | :163 / :28 |
| FloatCurve 에디터 | Editor\UI\Asset\Curve\FloatCurveEditorWidget.cpp:215 | `Float Curve Editor` + `###FloatCurveEditor` | :201 / :214(리터럴) |
| CameraShake 에디터 | Editor\UI\Asset\CameraShake\CameraShakeEditorWidget.cpp:403 | `Camera Shake Editor` + `###CameraShakeEditor` | :389 / :402(리터럴) |
| AnimGraph 에디터 | Editor\UI\Asset\Animation\AnimGraphEditorWidget.cpp:606 | `AnimGraph Editor##<Asset 포인터>` | snprintf :596-597 |
| 파티클 에디터 (부분) | Editor\UI\Asset\Particle\ParticleSystemEditorWidget.cpp:1495 | 표시명 = `GetParticleSystemTitle(에셋, IsDirty())` 런타임 동적 / ID = `###ParticleSystemEditor_<Id>` | VisibleTitle :1481 / WindowIdSuffix :1386 |
| 통계 오버레이 (1 Begin → 5 창) | Editor\Subsystem\OverlayStatSystem.cpp:466 | `Stat FPS` / `Stat Memory` / `Stat Shadow` / `Stat Skinning` / `Stat Particles` (각 `##Stat*Overlay` ID) | RenderWindow 호출 :487 / :494 / :501 / :508 / :515 |

**C+ 추적 결과: 대상 8건 전부 추적성공(추적불가 0).** 그중 리터럴 완전확정 6, 파티클은 부분(ID 확정·표시명 런타임 동적), 통계 오버레이는 단일 Begin이 실제 **5개 명명 창**으로 전개됨을 확인.

---

## 보강 자기점검

### 신규 진입점 합계
| 색인 | 신규 건수 | 비고 |
|---|---|---|
| B+ | **52** | 32 클래스; B와 중복 클래스 4(UEngine/UWorld/ULevel/AActor), 신규 클래스 28 |
| A+ | **65** | *Pass 18, *Pipeline 6, *SceneProxy 12, *ViewportClient 9, *Base 14, *AnimInstance 3, *Factory 3 / .cpp 58·헤더전용 7 |
| A++ | **0** | struct 매니저/서비스 없음 |
| C+ | (가산 안 함) | 기존 C1 항목의 라벨 해소. 단 통계 오버레이 1 Begin = 실제 5창 |

**보강 후 총 진입점 = 395 + 52 + 65 + 0 = 512.**
(실 노출 창 기준으로 통계 오버레이 5창을 펼치면 +4 → 516.)

### 여전히 어떤 grep 규칙으로도 안 잡히는 surface
1. **Lua 바인딩 게임 로직.** sol2 `new_usertype`/`set_function` 등으로 스크립트에 노출되는 함수군 — `Engine\Lua\LuaScriptManager.cpp`, `Game\Lua\GameLuaBindings.cpp`, `ULuaScriptComponent`/`ULuaAnimInstance`/`ALuaCharacter`. C++ 클래스·메서드명 규칙으로는 안 잡히고, 바인딩 등록 호출을 추적해야 함.
2. **RmlUi 런타임 UI.** `UUIManager`/`UUserWidget`가 로드하는 `.rml`/`.rcss` 문서·위젯은 데이터 기반(마크업)이라 C++ 색인 대상이 아님(ImGui 색인과도 별개 surface).
3. **Slate `S*` 위젯.** `SWidget`/`SWindow`/`SSplitter`/`SlateApplication` — A·A+ 어느 접미사에도 안 걸림.
4. **이번 규칙 밖 클래스 계열.** `*Instance`(AnimGraphInstance/AnimSingleNodeInstance/CharacterAnimGraphInstance), `*Module`(파티클 모듈군 UParticleModule*), `*Asset`, `*Notify`/`*NotifyState`, `*Modifier`(CameraModifier 등), `*Volume` — 일부는 B/B+로 간접 노출되나 클래스 색인으로는 미수집.
