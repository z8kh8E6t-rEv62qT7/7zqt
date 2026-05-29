import AppKit
import SwiftUI

final class QuickLookCollectionProxy {
  private var pendingInitialSyntheticRootProbeGeneration: UInt64?
  private var currentInitialSyntheticRootWidth: CGFloat?
  var onInitialSyntheticRootProbeWidthStable: ((UInt64) -> Void)?

  func beginInitialSyntheticRootProbe(generation: UInt64) {
    pendingInitialSyntheticRootProbeGeneration = generation
  }

  func cancelInitialSyntheticRootProbe() {
    pendingInitialSyntheticRootProbeGeneration = nil
  }

  func notifyInitialSyntheticRootProbeWidthStable(generation: UInt64) {
    guard pendingInitialSyntheticRootProbeGeneration == generation else {
      return
    }
    pendingInitialSyntheticRootProbeGeneration = nil
    onInitialSyntheticRootProbeWidthStable?(generation)
  }

  func recordInitialSyntheticRootItemWidth(_ width: CGFloat?) {
    guard let width, width.isFinite, width > 0 else {
      currentInitialSyntheticRootWidth = nil
      return
    }
    currentInitialSyntheticRootWidth = width
  }

  func currentInitialSyntheticRootItemWidth() -> CGFloat? {
    currentInitialSyntheticRootWidth
  }
}

private struct QuickLookBrowseSelectionContext {
  let index: Int
  let modifiers: QuickLookSelectionModifiers
  let allowsPrimaryAction: Bool
}

private struct QuickLookInitialSyntheticRootProbePreference: Equatable {
  let width: CGFloat?
  let pending: Bool
  let generation: UInt64?
}

private struct QuickLookInitialSyntheticRootProbePreferenceKey: PreferenceKey {
  static var defaultValue = QuickLookInitialSyntheticRootProbePreference(
    width: nil,
    pending: false,
    generation: nil)

  static func reduce(value: inout QuickLookInitialSyntheticRootProbePreference,
                     nextValue: () -> QuickLookInitialSyntheticRootProbePreference) {
    let next = nextValue()
    if next.pending {
      value = next
    }
  }
}

struct QuickLookCollectionContainer: View {
  let rows: [QuickLookListRowModel]
  let selection: IndexSet
  let initialSyntheticRootProbeGeneration: UInt64?
  let proxy: QuickLookCollectionProxy
  let onSelectionChange: (IndexSet) -> Void
  let onPrimaryAction: () -> Void

  @State private var hoveredIndex: Int?
  @State private var visibleWidth: CGFloat = 0
  @State private var activeModifiers = EventModifiers()
  @State private var selectionAnchorIndex: Int?
  @State private var pendingClick: QuickLookPendingLocalClick?
  @State private var observedInitialSyntheticRootProbeGeneration: UInt64?
  @State private var probeWidthStability = QuickLookVisibleWidthStability()
  @State private var probeWidthStabilityRecheckScheduled = false

  private let sectionInsets = EdgeInsets(top: 6, leading: 6, bottom: 6, trailing: 6)

  var body: some View {
    GeometryReader { geometry in
      let width = visibleContentWidth(from: geometry.size.width)
      ScrollView {
        ZStack(alignment: .topLeading) {
          Color.clear
            .contentShape(Rectangle())
            .onTapGesture {
              handleBackgroundClick()
            }

          LazyVStack(spacing: 2) {
            ForEach(Array(rows.enumerated()), id: \.element.id) { index, row in
              rowButton(row: row, index: index, rowWidth: rowWidthValue(for: width))
                .disabled(!isRowInteractive(at: index))
                .opacity(row.isPendingInitialReveal ? 0 : 1)
                .background(initialSyntheticRootProbeReporter(row: row, index: index))
            }
          }
          .padding(sectionInsets)
        }
        .frame(maxWidth: .infinity, minHeight: geometry.size.height, alignment: .topLeading)
      }
      .background(Color.clear)
      .onAppear {
        updateVisibleWidth(width)
        synchronizeSelectionAnchor()
        synchronizeInitialSyntheticRootProbeGeneration()
      }
      .onChange(of: width) { _, newWidth in
        updateVisibleWidth(newWidth)
      }
      .onChange(of: rows) { _, _ in
        sanitizeSelectionIfNeeded()
        synchronizeInitialSyntheticRootProbeGeneration()
      }
      .onChange(of: initialSyntheticRootProbeGeneration) { _, _ in
        synchronizeInitialSyntheticRootProbeGeneration()
      }
      .onChange(of: selection) { _, _ in
        synchronizeSelectionAnchor()
      }
      .onPreferenceChange(QuickLookInitialSyntheticRootProbePreferenceKey.self) { preference in
        handleInitialSyntheticRootProbePreference(preference)
      }
      .focusable(true)
      .onKeyPress(.return) {
        onPrimaryAction()
        return .handled
      }
      .onModifierKeysChanged(mask: [.command, .shift, .control, .option]) { _, newModifiers in
        activeModifiers = newModifiers
      }
    }
  }

  private func rowButton(row: QuickLookListRowModel, index: Int, rowWidth: CGFloat) -> some View {
    // Keep each file row as a real SwiftUI Button action inside the Quick Look
    // window. The ref_projects/quicklookdoubleclick repro showed that rapid
    // repeated clicks stay local on this path, while NSHostingView mouseDown
    // bridges, NSButton row bases, custom NSControl row bases, and CGEventTap
    // based designs can leak rapid clicks back to Finder or require unrelated
    // input-monitoring permission.
    QuickLookLocalButton(
      accessibilityLabel: row.title,
      action: {
        handleRowClick(at: index)
      }) { _ in
      QuickLookCollectionItemView(
        model: row,
        isSelected: selection.contains(index),
        isHovered: hoveredIndex == index,
        rowWidth: rowWidth)
    }
    .onHover { hovering in
      guard !row.isPendingInitialReveal else {
        if hoveredIndex == index {
          hoveredIndex = nil
        }
        return
      }
      if hovering {
        hoveredIndex = index
      } else if hoveredIndex == index {
        hoveredIndex = nil
      }
    }
  }

  private func handleRowClick(at index: Int) {
    guard let context = clickContext(for: index) else {
      clearPendingClick()
      return
    }

    let update = QuickLookSelectionStrategy.update(
      itemIndex: context.index,
      currentSelection: selection,
      anchorIndex: selectionAnchorIndex,
      itemCount: rows.count,
      modifiers: context.modifiers)
    selectionAnchorIndex = update.anchorIndex
    onSelectionChange(update.selection)

    guard context.allowsPrimaryAction else {
      clearPendingClick()
      return
    }

    switch QuickLookClickPairing.consume(
      rowID: rows[context.index].id,
      now: Date(),
      pendingClick: &pendingClick)
    {
    case .singleClick:
      break
    case .doubleClick:
      onPrimaryAction()
    }
  }

  private func handleBackgroundClick() {
    guard !hasUnsupportedClickModifiers(activeModifiers) else {
      clearPendingClick()
      return
    }

    clearPendingClick()
    selectionAnchorIndex = nil
    if !selection.isEmpty {
      onSelectionChange(IndexSet())
    }
  }

  private func clickContext(for index: Int) -> QuickLookBrowseSelectionContext? {
    guard isRowInteractive(at: index) else {
      return nil
    }
    guard !hasUnsupportedClickModifiers(activeModifiers) else {
      return nil
    }

    let selectionModifiers = selectionModifiers(for: activeModifiers)
    return QuickLookBrowseSelectionContext(
      index: index,
      modifiers: selectionModifiers,
      allowsPrimaryAction: selectionModifiers.isEmpty)
  }

  private func hasUnsupportedClickModifiers(_ modifiers: EventModifiers) -> Bool {
    modifiers.contains(.control) || modifiers.contains(.option)
  }

  private func selectionModifiers(for modifiers: EventModifiers) -> QuickLookSelectionModifiers {
    var selectionModifiers = QuickLookSelectionModifiers()
    if modifiers.contains(.shift) {
      selectionModifiers.insert(.shift)
    }
    if modifiers.contains(.command) {
      selectionModifiers.insert(.command)
    }
    return selectionModifiers
  }

  private func isRowInteractive(at index: Int) -> Bool {
    guard rows.indices.contains(index) else {
      return false
    }
    return !rows[index].isPendingInitialReveal
  }

  private func sanitizeSelectionIfNeeded() {
    var sanitized = IndexSet()
    for index in selection where rows.indices.contains(index) {
      sanitized.insert(index)
    }
    guard sanitized != selection else {
      return
    }
    onSelectionChange(sanitized)
  }

  private func synchronizeSelectionAnchor() {
    if let anchor = selectionAnchorIndex, selection.contains(anchor), rows.indices.contains(anchor) {
      return
    }
    selectionAnchorIndex = selection.max()
  }

  private func clearPendingClick() {
    pendingClick = nil
  }

  private func visibleContentWidth(from proposedWidth: CGFloat) -> CGFloat {
    guard proposedWidth.isFinite, proposedWidth > 0 else {
      return QuickLookRowLayoutMetrics.fallbackRowWidth
    }
    return proposedWidth
  }

  private func updateVisibleWidth(_ width: CGFloat) {
    guard abs(width - visibleWidth) > 0.5 else {
      return
    }
    visibleWidth = width
    observeInitialSyntheticRootProbeIfNeeded()
  }

  private func rowWidthValue(for visibleWidth: CGFloat) -> CGFloat {
    let horizontalInsets = sectionInsets.leading + sectionInsets.trailing
    return QuickLookRowLayoutMetrics.rowWidth(
      visibleWidth: visibleWidth,
      horizontalInsets: horizontalInsets)
  }

  private func initialSyntheticRootProbeReporter(row: QuickLookListRowModel,
                                                 index: Int) -> some View {
    GeometryReader { geometry in
      Color.clear.preference(
        key: QuickLookInitialSyntheticRootProbePreferenceKey.self,
        value: QuickLookInitialSyntheticRootProbePreference(
          width: geometry.size.width,
          pending: index == 0 && row.isPendingInitialReveal,
          generation: initialSyntheticRootProbeGeneration))
    }
  }

  private func synchronizeInitialSyntheticRootProbeGeneration() {
    guard let generation = initialSyntheticRootProbeGeneration else {
      if observedInitialSyntheticRootProbeGeneration != nil {
        resetInitialSyntheticRootProbeObservation()
      }
      return
    }

    if observedInitialSyntheticRootProbeGeneration != generation {
      observedInitialSyntheticRootProbeGeneration = generation
      probeWidthStability.reset()
      probeWidthStabilityRecheckScheduled = false
    }
    observeInitialSyntheticRootProbeIfNeeded()
    scheduleInitialSyntheticRootProbeRecheck()
  }

  private func handleInitialSyntheticRootProbePreference(
    _ preference: QuickLookInitialSyntheticRootProbePreference
  ) {
    guard preference.pending else {
      proxy.recordInitialSyntheticRootItemWidth(nil)
      return
    }
    proxy.recordInitialSyntheticRootItemWidth(preference.width)
    guard let width = preference.width, width.isFinite, width > 0 else {
      scheduleInitialSyntheticRootProbeRecheck()
      return
    }
    guard let generation = preference.generation else {
      return
    }
    observeInitialSyntheticRootProbeWidth(width, generation: generation)
  }

  private func observeInitialSyntheticRootProbeIfNeeded() {
    guard let generation = observedInitialSyntheticRootProbeGeneration else {
      return
    }

    guard isPendingInitialSyntheticRootRowVisible else {
      resetInitialSyntheticRootProbeObservation()
      return
    }

    guard let width = proxy.currentInitialSyntheticRootItemWidth() else {
      scheduleInitialSyntheticRootProbeRecheck()
      return
    }

    observeInitialSyntheticRootProbeWidth(width, generation: generation)
  }

  private func observeInitialSyntheticRootProbeWidth(_ width: CGFloat, generation: UInt64) {
    guard observedInitialSyntheticRootProbeGeneration == generation else {
      return
    }

    if probeWidthStability.observe(width: width) {
      resetInitialSyntheticRootProbeObservation()
      proxy.notifyInitialSyntheticRootProbeWidthStable(generation: generation)
      return
    }

    scheduleInitialSyntheticRootProbeRecheck()
  }

  private func scheduleInitialSyntheticRootProbeRecheck() {
    guard observedInitialSyntheticRootProbeGeneration != nil,
          !probeWidthStabilityRecheckScheduled
    else {
      return
    }

    probeWidthStabilityRecheckScheduled = true
    DispatchQueue.main.async {
      probeWidthStabilityRecheckScheduled = false
      observeInitialSyntheticRootProbeIfNeeded()
    }
  }

  private func resetInitialSyntheticRootProbeObservation() {
    observedInitialSyntheticRootProbeGeneration = nil
    probeWidthStability.reset()
    probeWidthStabilityRecheckScheduled = false
  }

  private var isPendingInitialSyntheticRootRowVisible: Bool {
    rows.indices.contains(0) && rows[0].isPendingInitialReveal
  }
}

private struct QuickLookCollectionItemView: View {
  let model: QuickLookListRowModel
  let isSelected: Bool
  let isHovered: Bool
  let rowWidth: CGFloat

  var body: some View {
    rowBody(
      timestampMinWidth: QuickLookRowLayoutMetrics.timestampMinWidth(
        forRowWidth: rowWidth))
  }

  private func rowBody(timestampMinWidth: CGFloat) -> some View {
    HStack(spacing: 14) {
      iconBadge

      VStack(alignment: .leading, spacing: 5) {
        Text(model.title)
          .font(model.isSyntheticArchiveRoot ? .system(size: 14, weight: .semibold) :
              .system(size: 13, weight: model.isArchiveLike ? .medium : .regular))
          .italic(model.isArchiveLike && !model.isDirectory)
          .lineLimit(1)
          .layoutPriority(2)

        HStack(spacing: 10) {
          Text(model.detailText)
            .font(.caption)
            .foregroundStyle(QuickLookPreviewTheme.secondaryText)
            .lineLimit(1)

          Text(model.sizeText)
            .font(.caption.monospacedDigit())
            .foregroundStyle(QuickLookPreviewTheme.secondaryText)
            .lineLimit(1)
            .layoutPriority(1)
        }
      }
      .frame(maxWidth: .infinity, alignment: .leading)
      .layoutPriority(2)

      Spacer(minLength: 16)

      Text(model.modifiedText)
        .font(.caption2.monospacedDigit())
        .foregroundStyle(QuickLookPreviewTheme.tertiaryText)
        .lineLimit(1)
        .frame(minWidth: timestampMinWidth, alignment: .trailing)
    }
    .padding(.horizontal, 16)
    .padding(.vertical, 12)
    .frame(maxWidth: .infinity, minHeight: QuickLookPreviewTheme.rowHeight, alignment: .leading)
    .background(
      RoundedRectangle(cornerRadius: QuickLookPreviewTheme.rowCornerRadius)
        .fill(backgroundFill))
    .overlay(
      RoundedRectangle(cornerRadius: QuickLookPreviewTheme.rowCornerRadius)
        .stroke(borderColor, lineWidth: 1))
    .contentShape(RoundedRectangle(cornerRadius: QuickLookPreviewTheme.rowCornerRadius))
    .animation(.easeOut(duration: 0.16), value: isHovered)
  }

  private var iconBadge: some View {
    ZStack {
      RoundedRectangle(cornerRadius: 12)
        .fill(iconBadgeFill)
      Image(nsImage: model.icon)
        .resizable()
        .aspectRatio(contentMode: .fit)
        .frame(width: 18, height: 18)
    }
    .frame(width: 34, height: 34)
  }

  private var backgroundFill: Color {
    if isSelected {
      return QuickLookPreviewTheme.accentTint
    }
    if isHovered {
      return QuickLookPreviewTheme.hoverTint
    }
    return .clear
  }

  private var borderColor: Color {
    if isSelected {
      return QuickLookPreviewTheme.accentStroke
    }
    if isHovered {
      return QuickLookPreviewTheme.hoverStroke
    }
    return QuickLookPreviewTheme.cardBorder
  }

  private var iconBadgeFill: Color {
    if isSelected {
      return Color.primary.opacity(0.09)
    }
    return isHovered ? Color.primary.opacity(0.07) : Color.primary.opacity(0.05)
  }
}
