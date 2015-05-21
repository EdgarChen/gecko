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
   * Get iccStatus.
   *
   * options:
   *   null
   *
   * response:
   *   iccStatus
   */
  ParcelHelperObject.prototype.getIccStatus = function(aOptions, aCallback) {
    let Buf = this.context.Buf;
    Buf.simpleRequest(REQUEST_GET_SIM_STATUS, aOptions, (aLength, aOptions) => {
      if (aOptions.errorMsg) {
        aCallback({errorMsg: aOptions.errorMsg});
        return;
      }

      let iccStatus = {};
      iccStatus.cardState = Buf.readInt32(); // CARD_STATE_*
      iccStatus.universalPINState = Buf.readInt32(); // CARD_PINSTATE_*
      iccStatus.gsmUmtsSubscriptionAppIndex = Buf.readInt32();
      iccStatus.cdmaSubscriptionAppIndex = Buf.readInt32();
      if (!this.v5Legacy) {
        iccStatus.imsSubscriptionAppIndex = Buf.readInt32();
      }

      let apps_length = Buf.readInt32();
      if (apps_length > CARD_MAX_APPS) {
        apps_length = CARD_MAX_APPS;
      }

      iccStatus.apps = [];
      for (let i = 0 ; i < apps_length ; i++) {
        iccStatus.apps.push({
          app_type:       Buf.readInt32(), // CARD_APPTYPE_*
          app_state:      Buf.readInt32(), // CARD_APPSTATE_*
          perso_substate: Buf.readInt32(), // CARD_PERSOSUBSTATE_*
          aid:            Buf.readString(),
          app_label:      Buf.readString(),
          pin1_replaced:  Buf.readInt32(),
          pin1:           Buf.readInt32(),
          pin2:           Buf.readInt32()
        });
        if (RILQUIRKS_SIM_APP_STATE_EXTRA_FIELDS) {
          Buf.readInt32();
          Buf.readInt32();
          Buf.readInt32();
          Buf.readInt32();
        }
      }

      aCallback({iccStatus: iccStatus});
    });
  };

  /**
   * Documentation ....
   *
   * Enter Icc Pin.
   *
   * options:
   *   An object contains following attribute:
   *   - password: String containing the PIN.
   *
   * response:
   *   An object contains following attribute:
   *   - retryCount
   */
  ParcelHelperObject.prototype.enterIccPin = function(aOptions, aCallback) {
    let Buf = this.context.Buf;
    Buf.newParcel(REQUEST_ENTER_SIM_PIN, aOptions, (aLength, aOptions) => {
      aOptions.retryCount = aLength ? Buf.readInt32List()[0] : -1;
      aCallback(aOptions);
    });

    // Remove v5Legacy.
    Buf.writeInt32(2);
    Buf.writeString(aOptions.password);
    Buf.writeString(aOptions.aid || this.context.RIL.aid);
    Buf.sendParcel();
  };

  /**
   * Documentation ....
   *
   * Enter Icc Puk.
   *
   * options:
   *   An object contains following attribute:
   *   - password: String containing the PUK value.
   *   - newPin: String containing the new PIN value.
   *
   * response:
   *   An object contains following attribute:
   *   - retryCount
   */
  ParcelHelperObject.prototype.enterIccPuk = function(aOptions, aCallback) {
    let Buf = this.context.Buf;
    Buf.newParcel(REQUEST_ENTER_SIM_PUK, aOptions, (aLength, aOptions) => {
      aOptions.retryCount = aLength ? Buf.readInt32List()[0] : -1;
      aCallback(aOptions);
    });

    // Remove v5Legacy.
    Buf.writeInt32(3);
    Buf.writeString(aOptions.password);
    Buf.writeString(aOptions.newPin);
    Buf.writeString(aOptions.aid || this.context.RIL.aid);
    Buf.sendParcel();
  };

  /**
   * Documentation ....
   *
   * Enter Icc Pin2.
   *
   * options:
   *   An object contains following attribute:
   *   - password: String containing the PIN2.
   *
   * response:
   *   An object contains following attribute:
   *   - retryCount
   */
  ParcelHelperObject.prototype.enterIccPin2 = function(aOptions, aCallback) {
    let Buf = this.context.Buf;
    Buf.newParcel(REQUEST_ENTER_SIM_PIN2, aOptions, (aLength, aOptions) => {
      aOptions.retryCount = aLength ? Buf.readInt32List()[0] : -1;
      aCallback(aOptions);
    });

    // Remove v5Legacy.
    Buf.writeInt32(2);
    Buf.writeString(aOptions.password);
    Buf.writeString(aOptions.aid || this.context.RIL.aid);
    Buf.sendParcel();
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

  /**
   * Documentation ....
   *
   * Get the preferred network type.
   *
   * options:
   *   null
   *
   * response:
   *   null
   */
  ParcelHelperObject.prototype.getPreferredNetworkType = function(aOptions, aCallback) {
    let Buf = this.context.Buf;
    Buf.simpleRequest(REQUEST_GET_PREFERRED_NETWORK_TYPE, aOptions, (aLength, aOptions) => {
      if (aOptions.errorMsg) {
        aCallback(aOptions);
        return;
      }

      aOptions.type = Buf.readInt32List()[0];
      aCallback(aOptions);
    });
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