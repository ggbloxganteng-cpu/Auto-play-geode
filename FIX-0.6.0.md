# Auto Trajectory v0.6.0 — touch dispatcher compile fix

Fixed `src/ui/FloatingPanelTouch.hpp` to use the modern Geode/Cocos touch dispatcher accessor:

```cpp
cocos2d::CCTouchDispatcher::get()
    ->addTargetedDelegate(this, -127, true);
```

No other gameplay logic was changed in this hotfix. The complete offline test suite (`tests/run.sh`) passes. A Geode/Android build was not run in this environment; compile against the project's target Geode SDK to verify the SDK integration.
