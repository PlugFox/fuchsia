// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math';
import 'package:quiver/iterables.dart' show zip;

import 'sl4f_client.dart';
import 'ssh.dart';

/// Clockwise rotation of the screen.
enum Rotation {
  degrees0,
  degrees90,
  degrees180,
  degrees270,
}

/// Send screen interactions to the device using Scenic.
///
/// This operates on a physical screen coordinate system where the top left is
/// (0,0) and the bottom right is (1000, 1000). It's possible to indicate how is
/// the screen rotated which will transform the coordinates so that the physical
/// (0,0) is on the top left.
class Input {
  final Ssh ssh;
  final Rotation _screenRotation;
  final Sl4f _sl4f;

  /// Construct an [Input] object.
  ///
  /// You can change the default [screenRotation] to compensate for.
  Input(Sl4f sl4f, [this._screenRotation = Rotation.degrees0])
      : ssh = sl4f.ssh,
        _sl4f = sl4f;

  /// Taps on the screen at coordinates ([coord.x], [coord.y]).
  ///
  /// Coordinates must be in the range of [0, 1000] and are scaled to the screen
  /// size on the device, and they are rotated to compensate for the clockwise
  /// [screenRotation].
  ///
  /// [tap_event_count]: Number of tap events to send ([duration] is divided
  /// over the tap events). Defaults to 1.
  /// [duration]: Duration of the event(s) in milliseconds. Defaults to 0.
  /// These defaults are set in the input facade.
  Future<bool> tap(Point<int> coord,
      {Rotation screenRotation, int tapEventCount, int duration}) async {
    final tcoord = _rotate(coord, screenRotation ?? _screenRotation);
    final result = await _sl4f.request('input_facade.Tap', {
      'x': tcoord.x,
      'y': tcoord.y,
      if (tapEventCount != null) 'tap_event_count': tapEventCount,
      if (duration != null) 'duration': duration,
    });
    return result == 'Success';
  }

  /// Multi-Finger taps on the screen.
  ///
  /// [fingers] are represented by a list of [Point]
  ///
  /// Each finger x, y must be in the range of [0, 1000] and
  /// are scaled to the screen size on the device, and they
  /// are rotated to compensate for the clockwise [screenRotation].
  ///
  /// [tap_event_count]: Number of tap events to send ([duration] is divided
  /// over the tap events). Defaults to 1.
  /// [duration]: Duration of the event(s) in milliseconds. Defaults to 0.
  /// These defaults are set in the input facade.
  Future<bool> multiFingerTap(List<Point<int>> fingers,
      {Rotation screenRotation, int tapEventCount, int duration}) async {
    // Convert each Point finger to Touch json matching the FIDL struct `Touch`
    // defined in sdk/fidl/fuchsia.ui.input/input_reports.fidl
    // Example:
    //   {'finger_id': 1, 'x': 0, 'y': 0, 'width': 0, 'height': 0}
    List<Map<String, int>> fingersJson = [];
    for (var i = 0; i < fingers.length; i++) {
      final tcoord = _rotate(fingers[i], screenRotation ?? _screenRotation);

      fingersJson.add({
        'finger_id': i + 1, // finger_id starts at 1.
        'x': tcoord.x,
        'y': tcoord.y,
        // width and height are required. We default them to 0 size.
        'width': 0,
        'height': 0,
      });
    }

    final result = await _sl4f.request('input_facade.MultiFingerTap', {
      'fingers': fingersJson,
      if (tapEventCount != null) 'tap_event_count': tapEventCount,
      if (duration != null) 'duration': duration,
    });
    return result == 'Success';
  }

  /// Swipes on the screen from coordinates ([from.x], [from.y]) to ([to.x],
  /// [to.y]).
  ///
  /// Coordinates must be in the range of [0, 1000] are scaled to the screen
  /// size on the device, and they are rotated to compensate for the clockwise
  /// [screenRotation]. How long the swipe lasts can be controlled with
  /// [duration].
  Future<bool> swipe(Point<int> from, Point<int> to,
      {Duration duration = const Duration(milliseconds: 300),
      Rotation screenRotation}) async {
    final tfrom = _rotate(from, screenRotation);
    final tto = _rotate(to, screenRotation);
    final result = await _sl4f.request('input_facade.Swipe', {
      'x0': tfrom.x,
      'y0': tfrom.y,
      'x1': tto.x,
      'y1': tto.y,
      'duration': duration.inMilliseconds,
    });
    return result == 'Success';
  }

  /// Swipes fingers from coordinates ([from[finger].x], [from[finger].y]) to
  /// ([to[finger].x], [to[finger].y]).
  ///
  /// Coordinates must be in the range of [0, 1000], are scaled to the screen
  /// size on the device, and they are rotated to compensate for the clockwise
  /// [screenRotation]. How long the swipe lasts can be controlled with
  /// [duration].
  ///
  /// The swipe will include a DOWN event, one MOVE event every 17 milliseconds,
  /// and an UP event. If [duration] is less than 17 milliseconds, no MOVE events
  /// will be generated.
  Future<bool> multiFingerSwipe(List<Point<int>> from, List<Point<int>> to,
      {Duration duration = const Duration(milliseconds: 300),
      Rotation screenRotation}) async {
    final tfrom = from.map((fingerFrom) => _rotate(fingerFrom, screenRotation));
    final tto = to.map((fingerTo) => _rotate(fingerTo, screenRotation));
    final fingers = zip([tfrom, tto])
        .map((fingerPos) => {
              'x0': fingerPos[0].x,
              'y0': fingerPos[0].y,
              'x1': fingerPos[1].x,
              'y1': fingerPos[1].y
            })
        .toList();
    final result = await _sl4f.request('input_facade.MultiFingerSwipe', {
      'fingers': fingers,
      'duration': duration.inMilliseconds,
    });
    return result == 'Success';
  }

  /// Enters [text], as if typed on a keyboard, with [keyEventDuration]
  /// between key events.
  ///
  /// [text] must be non-empty, and the characters within [text] must be
  /// representable using the current keyboard layout and locale.
  ///
  /// At present, it is assumed that the current layout and locale are
  /// "US-QWERTY" and "en-US", respectively.
  ///
  /// The number of events generated is [>= 2 * text.length]:
  /// * To account for both key-down and key-up events for every character.
  /// * To account for modifier keys (e.g. capital letters require pressing the
  ///   shift key).
  Future<bool> text(String text,
      {Duration keyEventDuration = const Duration(milliseconds: 1)}) async {
    final result = await _sl4f.request('input_facade.Text', {
      'text': text,
      'key_event_duration': keyEventDuration.inMilliseconds,
    });
    return result == 'Success';
  }

  /// Simulates a single key down + up sequence, for the given [hidUsageId],
  /// with [keyEventDuration] between key events.
  ///
  /// [hidUsageId] must be representable as an unsigned 16-bit integer.
  /// Otherwise, this method throws an [ArgumentError].
  ///
  /// [hidUsageId] will be interpreted as a "Usage ID" per
  //  "HID Usage Table Conventions" in
  /// https://www.usb.org/sites/default/files/documents/hut1_12v2.pdf,
  /// and will be interpreted in the context of "Usage Page" 0x07,
  /// which is the "Keyboard/Keypad" page.
  ///
  /// Because Usage IDs are defined by an external standard, it is impractical
  /// to perform detailed validation. Hence, any unsigned 16-bit value can be
  /// injected successfully. Interpretation of unrecognized values is subject to
  /// the choices of the system under test.
  ///
  /// Per fxbug.dev/63532, this method will be replaced with a method that deals in
  /// `fuchsia.input.Key`s, instead of HID Usage IDs.
  Future<bool> keyPress(int hidUsageId,
      {Duration keyPressDuration = const Duration(milliseconds: 1)}) async {
    const int maxHidUsageId = 0xFFFF;
    if (hidUsageId > maxHidUsageId) {
      throw new ArgumentError('hidUsageId is too large: $hidUsageId');
    } else if (hidUsageId < 0) {
      throw new ArgumentError('hidUsageId is negative: $hidUsageId');
    }

    final result = await _sl4f.request('input_facade.KeyPress', {
      'hid_usage_id': hidUsageId,
      'key_press_duration': keyPressDuration.inMilliseconds,
    });

    return result == 'Success';
  }

  /// Compensates for the given [screenRotation].
  ///
  /// If null is provided, the default specified in the constructor is used.
  Point<int> _rotate(Point<int> coord, Rotation screenRotation) {
    final rotation = screenRotation ?? _screenRotation;
    switch (rotation) {
      case Rotation.degrees0:
        return coord;
      case Rotation.degrees90:
        return Point<int>(1000 - coord.y, coord.x);
      case Rotation.degrees180:
        return Point<int>(1000 - coord.x, 1000 - coord.y);
      case Rotation.degrees270:
        return Point<int>(coord.y, 1000 - coord.x);
    }
    return coord;
  }
}
