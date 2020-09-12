// Work out the endpoint to use, for dev you can change to point at a remote ESP
// and run the HTML/JS from file, no need to upload to the ESP to test

var baseHost = window.location.hostname;
//var baseHost = 'emonesp.local';
//var baseHost = '192.168.4.1';
//var baseHost = '172.16.0.52';
var basePort = window.location.port;
var baseEndpoint = 'http://' + baseHost;
if(80 !== basePort) { 
  baseEndpoint += ":"+basePort;
}

var statusupdate = false;
var selected_network_ssid = "";
var lastmode = "";
var ipaddress = "";

// Convert string to number, divide by scale, return result
// as a string with specified precision
function scaleString(string, scale, precision) {
  var tmpval = parseInt(string) / scale;
  return tmpval.toFixed(precision);
}

function BaseViewModel(defaults, remoteUrl, mappings) {
  if(mappings === undefined){
   mappings = {};
  }
  var self = this;
  self.remoteUrl = remoteUrl;

  // Observable properties
  ko.mapping.fromJS(defaults, mappings, self);
  self.fetching = ko.observable(false);
}

BaseViewModel.prototype.update = function (after) {
  if(after === undefined){
   after = function () { };
  }
  var self = this;
  self.fetching(true);
  $.get(self.remoteUrl, function (data) {
    ko.mapping.fromJS(data, self);
  }, 'json').always(function () {
    self.fetching(false);
    after();
  });
};


function StatusViewModel() {
  var self = this;

  BaseViewModel.call(self, {
    "mode": "ERR",
    "networks": [],
    "rssi": [],
    "srssi": "",
    "ipaddress": "",
    "packets_sent": "",
    "packets_success": "",
    "emoncms_connected": "",
    "mqtt_connected": "",
    "free_heap": "",
    "rtc_set": false
  }, baseEndpoint + '/status');

  // Some devired values
  self.isWifiClient = ko.pureComputed(function () {
    return ("STA" == self.mode()) || ("STA+AP" == self.mode());
  });
  self.isWifiAccessPoint = ko.pureComputed(function () {
    return ("AP" == self.mode()) || ("STA+AP" == self.mode());
  });
  self.fullMode = ko.pureComputed(function () {
    switch (self.mode()) {
      case "AP":
        return "Access Point (AP)";
      case "STA":
        return "Client (STA)";
      case "STA+AP":
        return "Client + Access Point (STA+AP)";
    }

    return "Unknown (" + self.mode() + ")";
  });
}
StatusViewModel.prototype = Object.create(BaseViewModel.prototype);
StatusViewModel.prototype.constructor = StatusViewModel;

function ConfigViewModel() {
  BaseViewModel.call(this, {
    "ssid": "",
    "pass": "",
    "emoncms_server": "emoncms.org",
    "emoncms_path": "",
    "emoncms_apikey": "",
    "emoncms_node": "",
    "emoncms_fingerprint": "",
    "mqtt_server": "",
    "mqtt_topic": "",
    "mqtt_feed_prefix": "",
    "mqtt_user": "",
    "mqtt_pass": "",
    "www_username": "",
    "www_password": "",
    "postInterval": "",
    "vcalA": "",
    "icalA": "",
    "vcalB": "",
    "icalB": "",
    "chanA_VrefSet": "",
    "chanB_VrefSet": "",
    "channelA_gain": "",
    "channelB_gain": "",
    "R1_A": "",
    "R2_A": "",
    "R1_B": "",
    "R2_B": "",
    "Rshunt_A": "",
    "Rshunt_B": "",
    "AmpOffset_A": "",
    "AmpOffset_B": "",
    "VoltOffset_A": "",
    "VoltOffset_B": "",
    "BattType": "",
    "BattCapacity": "",
    "BattCapHr": "",
    "BattNom": "",
    "BattVoltsAlarmHigh": "",
    "BattVoltsAlarmLow": "",
    "BattPeukert": "",
    "BattTempCo": "",
    "espflash": "",
    "version": "0.0.0"
  }, baseEndpoint + '/config');
}
ConfigViewModel.prototype = Object.create(BaseViewModel.prototype);
ConfigViewModel.prototype.constructor = ConfigViewModel;

function LastValuesViewModel() {
  var self = this;
  self.remoteUrl = baseEndpoint + '/lastvalues';

  // Observable properties
  self.fetching = ko.observable(false);
  self.values = ko.mapping.fromJS([]);

  self.update = function (after) {
    if(after === undefined){
     after = function () { };
    }
    self.fetching(true);
    $.get(self.remoteUrl, function (data) {
      // Transform the data into something a bit easier to handle as a binding
      var namevaluepairs = data.split(",");
      var vals = [];
      for (var z in namevaluepairs) {
        var namevalue = namevaluepairs[z].split(":");
        var units = "";
        if (namevalue[0].indexOf("CT") === 0) units = "W";
        //if (namevalue[0].indexOf("T") === 0) units = String.fromCharCode(176)+"C";
        vals.push({key: namevalue[0], value: namevalue[1]+units});
      }
      ko.mapping.fromJS(vals, self.values);
    }, 'text').always(function () {
      self.fetching(false);
      after();
    });
  };
}

function LogsViewModel() {
  var self = this;
  self.remoteUrl = baseEndpoint + "/lastvalues";

  // Observable properties
  self.fetching = ko.observable(false);
  self.entries = ko.mapping.fromJS([]);

  let oldData = "";

  self.update = function (after) {
    if (after === undefined) {
      after = function () { };
    }
    self.fetching(true);

    $.get(
      self.remoteUrl,
      function (data) {
        if (data !== oldData) {
          var logEntries = self.entries.slice();
          logEntries.push({
            timestamp: new Date().toISOString(),
            log: data
          });

          ko.mapping.fromJS(logEntries, self.entries);
          oldData = data;
        }
      },
      "text"
    ).always(function () {
      self.fetching(false);
      after();
    });
  };
}


function StorageViewModel() {
  var self = this;
  var remoteUrl = baseEndpoint + '/sd';
  var mappings = {};

  self.fetching = ko.observable(false);
  self.dir = ko.observable("/");
  self.dir.subscribe(() => {
    self.update();
  });  
  self.files = ko.mapping.fromJS([], mappings);
  self.filesFiltered = ko.mapping.fromJS([], mappings);

  self.update = (after) => {
    if(after === undefined){
     after = function () { };
    }
    self.fetching(true);
    $.get(remoteUrl+self.dir(), function (data) {
      data = data.sort(function (left, right) {
        return left.name < right.name ? -1 : 1;
      });

      ko.mapping.fromJS(data, self.files);

      const uniqueArray = data.filter((file, index) => {
        return !file.hidden && !file.directory;
      }).sort(function (left, right) {
        return left.name < right.name ? 1 : -1;
      });

      ko.mapping.fromJS(uniqueArray, self.filesFiltered);
    }, 'json').always(function () {
      self.fetching(false);
      after();
    });
  };
}

function EmonEspViewModel() {
  var self = this;

  self.config = new ConfigViewModel();
  self.status = new StatusViewModel();
  self.last = new LastValuesViewModel();
  self.logs = new LogsViewModel();
  self.storage = new StorageViewModel();
  self.downloadFile = ko.observable('');

  self.initialised = ko.observable(false);
  self.updating = ko.observable(false);

  var updateTimer = null;
  var updateTime = 1 * 1000;

  var logUpdateTimer = null;
  var logUpdateTime = 2000;

  // Upgrade URL
  self.upgradeUrl = ko.observable('about:blank');

  // -----------------------------------------------------------------------
  // Initialise the app
  // -----------------------------------------------------------------------
  self.start = function () {
    self.updating(true);
    self.config.update(function () {
      self.status.update(function () {
        self.last.update(function () {
          self.initialised(true);

          self.storage.update();

          updateTimer = setTimeout(self.update, updateTime);
          logUpdateTimer = setTimeout(self.updateLogs, logUpdateTime);

          // self.upgradeUrl(baseEndpoint + '/update');
          self.updating(false);
        });
      });
    });
  };

  // -----------------------------------------------------------------------
  // Get the updated state from the ESP
  // -----------------------------------------------------------------------
  self.update = function () {
    if (self.updating()) {
      return;
    }
    self.updating(true);
    if (null !== updateTimer) {
      clearTimeout(updateTimer);
      updateTimer = null;
    }
    self.status.update(function () 
    {
      // Time is not set, set from our local time
      if(false === self.status.rtc_set())
      {
        var newTime = new Date();
        $.post(baseEndpoint + "/settime", {
          "time": newTime.toISOString()
        }, () => {
        });
      }
      self.last.update(function () {
        updateTimer = setTimeout(self.update, updateTime);
        self.updating(false);
      });
    });
  };

  self.updateLogs = function () {
    if (null !== logUpdateTimer) {
      clearTimeout(logUpdateTimer);
      logUpdateTimer = null;
    }
    self.logs.update(function () {
      logUpdateTimer = setTimeout(self.updateLogs, logUpdateTime);
    });
  };

  self.wifiConnecting = ko.observable(false);
  self.status.mode.subscribe(function (newValue) {
    if(newValue === "STA+AP" || newValue === "STA") {
      self.wifiConnecting(false);
    }
  });

  // -----------------------------------------------------------------------
  // Event: WiFi Connect
  // -----------------------------------------------------------------------
  self.saveNetworkFetching = ko.observable(false);
  self.saveNetworkSuccess = ko.observable(false);
  self.saveNetwork = function () {
    if (self.config.ssid() === "") {
      alert("Please select network");
    } else {
      self.saveNetworkFetching(true);
      self.saveNetworkSuccess(false);
      $.post(baseEndpoint + "/savenetwork", { ssid: self.config.ssid(), pass: self.config.pass() }, function (data) {
          self.saveNetworkSuccess(true);
          self.wifiConnecting(true);
        }).fail(function () {
          alert("Failed to save WiFi config");
        }).always(function () {
          self.saveNetworkFetching(false);
        });
    }
  };

  // -----------------------------------------------------------------------
  // Event: Admin save
  // -----------------------------------------------------------------------
  self.saveAdminFetching = ko.observable(false);
  self.saveAdminSuccess = ko.observable(false);
  self.saveAdmin = function () {
    self.saveAdminFetching(true);
    self.saveAdminSuccess(false);
    $.post(baseEndpoint + "/saveadmin", { user: self.config.www_username(), pass: self.config.www_password() }, function (data) {
      self.saveAdminSuccess(true);
    }).fail(function () {
      alert("Failed to save Admin config");
    }).always(function () {
      self.saveAdminFetching(false);
    });
  };

  // -----------------------------------------------------------------------
  // Event: EmonDC save
  // -----------------------------------------------------------------------
  self.saveEmonDCFetching = ko.observable(false);
  self.saveEmonDCSuccess = ko.observable(false);
  self.saveEmonDC = function () {
    self.saveEmonDCFetching(true);
    self.saveEmonDCSuccess(false);
    $.post(baseEndpoint + "/savedc", { interval: self.config.postInterval(), vcalA: self.config.vcalA(), icalA: self.config.icalA(), vcalB: self.config.vcalB(), icalB: self.config.icalB(),  chanA_VrefSet: self.config.chanA_VrefSet(), chanB_VrefSet: self.config.chanB_VrefSet(), channelA_gain: self.config.channelA_gain(), channelB_gain: self.config.channelB_gain(), R1_A: self.config.R1_A(), R2_A: self.config.R2_A(), R1_B: self.config.R1_B(), R2_B: self.config.R2_B(), Rshunt_A: self.config.Rshunt_A(), Rshunt_B: self.config.Rshunt_B(), AmpOffset_A: self.config.AmpOffset_A(), AmpOffset_B: self.config.AmpOffset_B(), VoltOffset_A: self.config.VoltOffset_A(), VoltOffset_B: self.config.VoltOffset_B(), BattType: self.config.BattType(), BattCapacity: self.config.BattCapacity(), BattCapHr: self.config.BattCapHr(), BattNom: self.config.BattNom(), BattVoltsAlarmHigh: self.config.BattVoltsAlarmHigh(), BattVoltsAlarmLow: self.config.BattVoltsAlarmLow(), BattPeukert: self.config.BattPeukert(), BattTempCo: self.config.BattTempCo() }, function (data) {
      self.saveEmonDCSuccess(true);
    }).fail(function () {
      alert("Failed to save config");
    }).always(function () {
      self.saveEmonDCFetching(false);
    });
  };

  // -----------------------------------------------------------------------
  // Event: Emoncms save
  // -----------------------------------------------------------------------
  self.saveEmonCmsFetching = ko.observable(false);
  self.saveEmonCmsSuccess = ko.observable(false);
  self.saveEmonCms = function () {
    var emoncms = {
      server: self.config.emoncms_server(),
      path: self.config.emoncms_path(),
      apikey: self.config.emoncms_apikey(),
      node: self.config.emoncms_node(),
      fingerprint: self.config.emoncms_fingerprint()
    };

    if (emoncms.server === "" || emoncms.node === "") {
      alert("Please enter Emoncms server and node");
    } else if (emoncms.apikey.length != 32) {
      alert("Please enter valid Emoncms apikey");
    } else if (emoncms.fingerprint !== "" && emoncms.fingerprint.length != 59) {
      alert("Please enter valid SSL SHA-1 fingerprint");
    } else {
      self.saveEmonCmsFetching(true);
      self.saveEmonCmsSuccess(false);
      $.post(baseEndpoint + "/saveemoncms", emoncms, function (data) {
        self.saveEmonCmsSuccess(true);
      }).fail(function () {
        alert("Failed to save Admin config");
      }).always(function () {
        self.saveEmonCmsFetching(false);
      });
    }
  };

  // -----------------------------------------------------------------------
  // Event: MQTT save
  // -----------------------------------------------------------------------
  self.saveMqttFetching = ko.observable(false);
  self.saveMqttSuccess = ko.observable(false);
  self.saveMqtt = function () {
    var mqtt = {
      server: self.config.mqtt_server(),
      topic: self.config.mqtt_topic(),
      prefix: self.config.mqtt_feed_prefix(),
      user: self.config.mqtt_user(),
      pass: self.config.mqtt_pass()
    };

    if (mqtt.server === "") {
      alert("Please enter MQTT server");
    } else {
      self.saveMqttFetching(true);
      self.saveMqttSuccess(false);
      $.post(baseEndpoint + "/savemqtt", mqtt, function (data) {
        self.saveMqttSuccess(true);
      }).fail(function () {
        alert("Failed to save MQTT config");
      }).always(function () {
        self.saveMqttFetching(false);
      });
    }
  };
}

$(function () {
  // Activates knockout.js
  var emonesp = new EmonEspViewModel();
  ko.applyBindings(emonesp);
  emonesp.start();
});

// -----------------------------------------------------------------------
// Event: Turn off Access Point
// -----------------------------------------------------------------------
document.getElementById("apoff").addEventListener("click", function (e) {

  var r = new XMLHttpRequest();
  r.open("POST", "apoff", true);
  r.onreadystatechange = function () {
    if (r.readyState != 4 || r.status != 200)
      return;
    var str = r.responseText;
    console.log(str);
    document.getElementById("apoff").style.display = 'none';
    if (ipaddress !== "")
      window.location = "http://" + ipaddress;
  };
  r.send();
});

// -----------------------------------------------------------------------
// Event: Reset config and reboot
// -----------------------------------------------------------------------
document.getElementById("reset").addEventListener("click", function (e) {

  if (confirm("CAUTION: Do you really want to Factory Reset? All setting and config will be lost.")) {
    var r = new XMLHttpRequest();
    r.open("POST", "reset", true);
    r.onreadystatechange = function () {
      if (r.readyState != 4 || r.status != 200)
        return;
      var str = r.responseText;
      console.log(str);
      if (str !== 0)
        document.getElementById("reset").innerHTML = "Resetting...";
    };
    r.send();
  }
});

// -----------------------------------------------------------------------
// Event: Restart
// -----------------------------------------------------------------------
document.getElementById("restart").addEventListener("click", function (e) {

  if (confirm("Restart emonESP? Current config will be saved, takes approximately 10s.")) {
    var r = new XMLHttpRequest();
    r.open("POST", "restart", true);
    r.onreadystatechange = function () {
      if (r.readyState != 4 || r.status != 200)
        return;
      var str = r.responseText;
      console.log(str);
      if (str !== 0)
        document.getElementById("reset").innerHTML = "Restarting";
    };
    r.send();
  }
});

// -----------------------------------------------------------------------
// Event: Download File
// -----------------------------------------------------------------------

// the downloader is not functional!
// https://stackoverflow.com/questions/22724070/prompt-file-download-with-xmlhttprequest
// -----------------------------------------------------------------------
/*
document.getElementById("download").addEventListener("click", function (e) {

var r = new XMLHttpRequest();
r.open("GET", "download", true);
r.responseType = 'blob';
r.onreadystatechange = function () {
  var blob = this.response;
  var contentDispo = this.getResponseHeader('Content-Disposition');
  // https://stackoverflow.com/a/23054920/
  // var fileName = contentDispo.match(/filename[^;=\n]*=((['"]).*?\2|[^;\n]*)/)[1];
  saveBlob(blob, "datalog.csv");
}
r.send();
});

function saveBlob(blob, fileName) {
  var a = document.createElement('a');
  a.href = window.URL.createObjectURL(blob);
  a.download = fileName;
  a.dispatchEvent(new MouseEvent('click'));
}
*/
// r.onreadystatechange = function () {
//   //if (r.readyState != 4 || r.status != 200)
//     //return;
//   //var str = r.responseText;
//   //console.log(str);
//   //if (str !== 0)
//     document.getElementById("download").innerHTML = "Downloading";
// };
// r.send();

//});
// -----------------------------------------------------------------------

// https://github.com/boblemaire/ESPAsyncSDWebServer/blob/master/SDcard/index.htm
// -----------------------------------------------------------------------
// function loadDownload(path){
//   document.getElementById('download-frame').src = path+"?download=true";
// }

// document.getElementById("download").addEventListener("click", function (e) {
  
//   download.onclick = function(e){
//     loadDownload(path);
//     if(document.body.getElementsByClassName('contextMenu').length > 0) document.body.removeChild(el);
//   };
// -----------------------------------------------------------------------



// -----------------------------------------------------------------------
// Event:Upload Firmware
// -----------------------------------------------------------------------
document.getElementById("submit-firmware").addEventListener("click", function(e) {
  if (confirm("Flashing takes a minute.\nOnly flash with compatible .bin file.")) {
  }
  else {
  e.preventDefault();
  }
});



function toggle(id) {
  var e = document.getElementById(id);
  if(e.style.display == 'block')
     e.style.display = 'none';
  else
     e.style.display = 'block';
}

