// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Background Fetch Id to use when its value is not significant.
const kBackgroundFetchId = 'bg-fetch-id';

function RegisterServiceWorker() {
  navigator.serviceWorker.register('sw.js').then(() => {
    sendResultToTest('ok - service worker registered');
  }).catch(sendErrorToTest);
}

// Starts a Background Fetch request for a single to-be-downloaded file.
function StartSingleFileDownload() {
  navigator.serviceWorker.ready.then(swRegistration => {
    return swRegistration.backgroundFetch.fetch(
        kBackgroundFetchId, [ '/notifications/icon.png' ]);
  }).then(bgFetchRegistration => {
    sendResultToTest('ok');
  }).catch(sendErrorToTest);
}
