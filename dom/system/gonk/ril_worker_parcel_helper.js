/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/* global DEBUG, DEBUG_WORKER */

"use strict";

/**
 * ........
 */
(function(exports) {

  // Set to true in ril_consts.js to see debug messages
  let DEBUG = DEBUG_WORKER;

  let ParcelHelperObject = function(aContext) {
    this.context = aContext;
  };

  ParcelHelperObject.prototype = {
    context: null,

    // write parcel to buf
    send: function(aRilMessageType, aOptions, aCallback) {
      let method = this[aRilMessageType];
      if (typeof method != "function") {
        // Unable to handle such type.
        aCallback({errorMsg: GECKO_ERROR_UNSPECIFIED_ERROR});
        return;
      }
      method.call(this, aOptions, aCallback);
    },

    // read parcel from buf
    onReceive: function(aType, aLength) {
      let method = this[aType];
      if (typeof method != "function") {
        // Unable to handle such type.
        return;
      }
      method.call(this, aLength);
    },
  };

  /**
   * Documentation ....
   *
   * Set the preferred network type.
   *
   * options:
   *   An object contains a valid value of
   *   RIL_PREFERRED_NETWORK_TYPE_TO_GECKO as its `type` attribute.
   *
   * response:
   *   null
   */
  ParcelHelperObject.prototype.setPreferredNetworkType = function(aOptions, aCallback) {
    let networkType = aOptions.type;
    if (networkType < 0 || networkType >= RIL_PREFERRED_NETWORK_TYPE_TO_GECKO.length) {
      aCallback({errorMsg: GECKO_ERROR_INVALID_PARAMETER});
      return;
    }

    let Buf = this.context.Buf;
    Buf.newParcel(REQUEST_SET_PREFERRED_NETWORK_TYPE, aOptions, (aLength, aOptions) => {
      aCallback(aOptions);
    });

    Buf.writeInt32(1);
    Buf.writeInt32(networkType);
    Buf.sendParcel();
  };

  // Solicited parcels.
  //ParcelHelperObject.prototype.foo = function ....

  // Unsolicited parcels.
  //ParcelHelperObject.prototype.[UNSOLICITED_CDMA_PRL_CHANGED] = function ....

  // Before we make sure to form it as a module would not add extra
  // overhead of module loading, we need to define it in this way
  // rather than 'module.exports' it as a module component.
  exports.ParcelHelperObject = ParcelHelperObject;
})(self); // in worker self is the global