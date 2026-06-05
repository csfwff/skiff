import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:skiff/scroll_button_overlay.dart';

void main() {
  testWidgets('scroll buttons trigger their callbacks', (tester) async {
    var upCount = 0;
    var downCount = 0;

    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(
          body: Center(
            child: ScrollButtonOverlay(
              onScrollUp: () => upCount++,
              onScrollDown: () => downCount++,
            ),
          ),
        ),
      ),
    );

    await tester.tap(find.byIcon(Icons.arrow_upward));
    await tester.pump();
    await tester.tap(find.byIcon(Icons.arrow_downward));
    await tester.pump();

    expect(upCount, 1);
    expect(downCount, 1);
  });
}
