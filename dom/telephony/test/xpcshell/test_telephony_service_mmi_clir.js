/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

function run_test() {
  run_next_test();
}

function newTelephonyService(aFakeSendRilWorker) {
  subscriptLoader.loadSubScript("resource://gre/components/TelephonyService.js", this);
  this.debug = do_print;
  let TS = new TelephonyService();
  TS._sendToRilWorker = aFakeSendRilWorker;
  return TS;
}

function do_dailMmi(aMmi, aRilResponse, aCallback) {
  let telephonyService = newTelephonyService(function(aClientId, aType, aMessage, aCallback) {
    aCallback(aRilResponse);
  });
  telephonyService._dialMMI(0, aMmi, aCallback);
}

function createMMIOptions(aProcedure, aServiceCode, aSia, aSib, aSic) {
  let mmi = {
    fullMMI: Array.slice(arguments).join("*") + "#",
    procedure: aProcedure,
    serviceCode: aServiceCode,
    sia: aSia,
    sib: aSib,
    sic: aSic
  };

  return mmi;
}

add_test(function test_clir() {
  let mmi = createMMIOptions("*#", "31");
  let telephonyService = newTelephonyService(function(aClientId, aType, aMessage, aCallback) {
    is(aType, "getCLIR");
    aCallback({errorMsg: "test"});
  });

  telephonyService._dialMMI(0, mmi, {
    notifyDialMMIError: function(errorMsg) {
      is(errorMsg, "test");
      //run_next_test();
    }
  });
});