/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

MARIONETTE_TIMEOUT = 60000;
MARIONETTE_HEAD_JS = "head.js";

// Password is hardcoded as "0000" by default in emulator.
const PASSWORD = "0000";
// Emulator doesn't support BA_ALL(330), BA_MO(333), BA_MT(353).
const CB_TYPES = ["33", "331", "332", "35", "351"];
const CB_TYPES_UNSUPPORTED = ["330", "333", "353"];
// Basic Service - 10: Voice + Fax + SMS.
const BS = "10";
const MMI_SERVICE_CLASS = ["serviceClassVoice", "serviceClassFax",
                           "serviceClassSms"];
// Call barring doesn't support Registration (**) and Erasure (##) operation.
const OPERATION_UNSUPPORTED = ["**", "##"];

function sendCbMMI(aOperation, aType, aExpectedSuccess, aExpectedStatusMessage) {
  let mmi = aOperation + aType + "*" + PASSWORD + "*" + BS + "#";
  log("Test " + mmi + " ...");

  return gSendMMI(mmi)
    .then((aResult) => {
      is(aResult.success, aExpectedSuccess, "Check success");
      is(aResult.serviceCode, "scCallBarring", "Check serviceCode");
      is(aResult.statusMessage, aExpectedStatusMessage,  "Check statusMessage");
      return aResult;
    });
}

function testCallBarring(aEnabled) {
  let promise = Promise.resolve();

  for (let i = 0; i < CB_TYPES.length; i++) {
    let type = CB_TYPES[i];
    promise = promise
      // Test setting call barring.
      .then(() => sendCbMMI(aEnabled ? "*" : "#", type, true,
                            aEnabled ? "smServiceEnabled" : "smServiceDisabled"))
      // Test getting call barring.
      .then(() => sendCbMMI("*#", type, true,
                            aEnabled ? "smServiceEnabledFor": "smServiceDisabled"))
      .then(aResult => {
        if (aEnabled) {
          for (let i = 0; i < MMI_SERVICE_CLASS.length; i++) {
            is(aResult.additionalInformation[i], MMI_SERVICE_CLASS[i],
               "Check additionalInformation[" + i + "]");
          }
        }
      });
  }

  return promise;
}

function testUnsupportType() {
  let promise = Promise.resolve();

  for (let i = 0; i < CB_TYPES_UNSUPPORTED.length; i++) {
    let type = CB_TYPES_UNSUPPORTED[i];
    promise = promise
      // Test setting call barring.
      .then(() => sendCbMMI("*", type, false, "RequestNotSupported"))
      // Test getting call barring.
      .then(() => sendCbMMI("*#", type, false, "RequestNotSupported"));
  }

  return promise;
}

function testUnsupportedOperation() {
  let promise = Promise.resolve();

  let types = CB_TYPES.concat(CB_TYPES_UNSUPPORTED);
  for (let i = 0; i < types.length; i++) {
    for (let j = 0; j < OPERATION_UNSUPPORTED.length; j++) {
      promise = promise
        .then(() => sendCbMMI(OPERATION_UNSUPPORTED[j], types[i], false,
                              "emMmiErrorNotSupported"));
    }
  }

  return promise;
}

// Start test.
startTest(function() {
  // Activate call barring service.
  return testCallBarring(true)
    // Deactivate call barring service.
    .then(() => testCallBarring(false))
    // Test unsupported call barring types.
    .then(() => testUnsupportType())
    // Test unsupported operation.
    .then(() => testUnsupportedOperation())

    .catch(error => ok(false, "Promise reject: " + error))
    .then(finish);;
});
