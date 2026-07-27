export function getTrackGeometry(track) {
    const rect = track.getBoundingClientRect();
    return { left: rect.left, width: rect.width };
}

export function capturePointer(track, pointerId) {
    if (!track.hasPointerCapture(pointerId))
        track.setPointerCapture(pointerId);
}

export function releasePointer(track, pointerId) {
    if (track.hasPointerCapture(pointerId))
        track.releasePointerCapture(pointerId);
}
