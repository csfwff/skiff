import 'package:flutter/material.dart';
import 'package:window_manager/window_manager.dart';

/// 悬浮滚动按钮组件
/// 三区域布局：▲ 滚动 | ≡ 拖拽 | ▼ 滚动
class ScrollButtonOverlay extends StatefulWidget {
  final VoidCallback onScrollUp;
  final VoidCallback onScrollDown;

  const ScrollButtonOverlay({
    super.key,
    required this.onScrollUp,
    required this.onScrollDown,
  });

  @override
  State<ScrollButtonOverlay> createState() => _ScrollButtonOverlayState();
}

class _ScrollButtonOverlayState extends State<ScrollButtonOverlay> {
  late FocusNode _focusNode;
  bool _hoverUp = false, _hoverHandle = false, _hoverDown = false;

  @override
  void initState() {
    super.initState();
    _focusNode = FocusNode(canRequestFocus: false);
  }

  @override
  void dispose() {
    _focusNode.dispose();
    super.dispose();
  }

  void _scrollUp() {
    debugPrint('▲ 向上点击已触发');
    widget.onScrollUp();
  }

  void _scrollDown() {
    debugPrint('▼ 向下点击已触发');
    widget.onScrollDown();
  }

  @override
  Widget build(BuildContext context) {
    return Focus(
      focusNode: _focusNode,
      canRequestFocus: false,
      child: Material(
        color: Colors.transparent,
        child: Container(
          width: 50,
          height: 110,
          decoration: BoxDecoration(
            color: const Color.fromRGBO(40, 40, 40, 0.7),
            borderRadius: BorderRadius.circular(12),
            boxShadow: const [
              BoxShadow(
                color: Colors.black38,
                blurRadius: 8,
                offset: Offset(0, 2),
              ),
            ],
          ),
          child: Column(
            children: [
              // ── ▲ 上区域：向上滚动 ──
              Expanded(
                child: InkWell(
                  onTap: _scrollUp,
                  borderRadius: const BorderRadius.vertical(
                    top: Radius.circular(12),
                  ),
                  hoverColor: const Color.fromRGBO(60, 60, 60, 0.8),
                  child: MouseRegion(
                    onEnter: (_) => setState(() => _hoverUp = true),
                    onExit: (_) => setState(() => _hoverUp = false),
                    cursor: SystemMouseCursors.click,
                    child: ColoredBox(
                      color: _hoverUp
                          ? const Color.fromRGBO(60, 60, 60, 0.8)
                          : Colors.transparent,
                      child: const Center(
                        child: Icon(Icons.arrow_upward,
                            color: Colors.white70, size: 20),
                      ),
                    ),
                  ),
                ),
              ),
              // ── 分割线 ──
              const Divider(height: 0.5, color: Colors.white24),
              // ── ≡ 中区域：拖拽手柄 ──
              GestureDetector(
                onPanStart: (_) {
                  debugPrint('≡ 拖拽开始');
                  windowManager.startDragging();
                },
                child: MouseRegion(
                  onEnter: (_) => setState(() => _hoverHandle = true),
                  onExit: (_) => setState(() => _hoverHandle = false),
                  cursor: SystemMouseCursors.grab,
                  child: Container(
                    height: 36,
                    color: _hoverHandle
                        ? const Color.fromRGBO(70, 70, 70, 0.8)
                        : Colors.transparent,
                    child: const Center(
                      child: Icon(Icons.drag_handle,
                          color: Colors.white54, size: 16),
                    ),
                  ),
                ),
              ),
              // ── 分割线 ──
              const Divider(height: 0.5, color: Colors.white24),
              // ── ▼ 下区域：向下滚动 ──
              Expanded(
                child: InkWell(
                  onTap: _scrollDown,
                  borderRadius: const BorderRadius.vertical(
                    bottom: Radius.circular(12),
                  ),
                  hoverColor: const Color.fromRGBO(60, 60, 60, 0.8),
                  child: MouseRegion(
                    onEnter: (_) => setState(() => _hoverDown = true),
                    onExit: (_) => setState(() => _hoverDown = false),
                    cursor: SystemMouseCursors.click,
                    child: ColoredBox(
                      color: _hoverDown
                          ? const Color.fromRGBO(60, 60, 60, 0.8)
                          : Colors.transparent,
                      child: const Center(
                        child: Icon(Icons.arrow_downward,
                            color: Colors.white70, size: 20),
                      ),
                    ),
                  ),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}
