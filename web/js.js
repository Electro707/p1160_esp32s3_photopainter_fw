import { PALETTES, loadImage, createCanvas, ditherImage, ditheredImgToBytes } from './imgProc.js';

// @ts-check
// helper to get the entry from a "frame"

/** @type {number|null} */
let pmicRefreshObj = null;      

const getEnt = (/** @type {string} */ id) => {
    const frm = document.getElementById(id);
    if(!frm){console.error(`element with id ${id} is null`); return null;}
    const ent = frm.getElementsByClassName("ent")[0];
    return ent;
};

const boolToShownStr = (/** @type {boolean} */ b) => {
    return b ? "Yes" : "No";
};

// after page is loaded, get some info from the ESP32
// todo: have this be called after
document.addEventListener("DOMContentLoaded", async () => {
    createEntryFrame('ent_wifiMode', "Current Wifi Mode", 'label');
    createEntryFrame('ent_wifiSSID', "SSID", 'input');
    createEntryFrame('ent_wifiPass', "Password", 'input');

    createEntryFrame('ent_pmic_batV', "Battery Voltage", 'label');
    createEntryFrame('ent_pmic_sysV', "System Voltage", 'label');
    createEntryFrame('ent_pmic_usbV', "USB Voltage", 'label');
    createEntryFrame('ent_pmic_batPe', "Battery Percentage", 'label');
    createEntryFrame('ent_pmic_vbusOk', "Is VBus OK?", 'label');
    createEntryFrame('ent_pmic_battPres', "Is Battery Present?", 'label');
    createEntryFrame('ent_pmic_currLim', "Is Current Limit?", 'label');

    createEntryFrame('ent_mode', "Mode", 'option',
        {options: [["standby", "Standby"], ["playlist", "Playlist"]]}
    );
    createEntryFrame('ent_playlist_mode', "Playlist Mode", 'option',
        {options: [["all", "All"], ["select", "Select Some"], ["random", "Random"]]}
    );
    createEntryFrame('ent_playlist_dur', "Cycle Duration (min)", 'input');
    

    // fetch the firmware version and put it to be processed
    apiGetVersion();
    apiGetWifiInfo();
    apiGetPmicInfo();
    apiGetMode();
    apiGetImgList();

    getEnt('ent_mode').addEventListener('change', changeModeSel);

    document.getElementById('bt_mode_set').onclick = clickSetMode;

    document.getElementById('chk_pmic_autoRefresh').addEventListener('change', function() {
        if (this.checked) {
            pmicRefreshObj = setInterval(() => {
                console.log("Making async pmic request");
                apiGetPmicInfo();
            }, 5000);
        } else {
            if(pmicRefreshObj != null){
                clearInterval(pmicRefreshObj);
                pmicRefreshObj = null;
            }
        }
    });

    document.getElementById('frm_fileInput').addEventListener('change', clickAddImgPart2);
    document.getElementById('bt_img_add').onclick = clickAdddImg;
});

/**
 * Creates a data frame, which contains a label plus some text
 * 
 * @param {string} frameId
 * @param {string} labelText
 * @param {'label'|'input'|'option'} entType
 * @param {null|{options?: [string, string][]}} [args]
 */
function createEntryFrame(frameId, labelText, entType, args = null){
    const divElem = document.getElementById(frameId);
    if(!divElem){
        console.error(`Frame for ID ${frameId} is null`);
        return;
    }
    var ent = "";

    switch(entType){
        case 'label':
            ent = `<span class="ent">-</span>`;
            break;
        case 'input':
            ent = `<input type="text" class="ent" placeholder="-">`;
            break;
        case 'option':
            if(args === null || !args['options']){
                console.error("No extra args grabbed!");
                break;
            }
            ent = `<select class="ent">`
            args['options'].forEach(e => {
                ent += `<option value="${e[0]}">${e[1]}</option>`
            });
            ent += `</select>`
            break;
        default:
            console.error(`Invalid entry type ${entType}`);
            break;
    }

    divElem.innerHTML = `<span class="lb">${labelText}: </span>${ent}`;
}

function changeModeSel(){
    const selVal = getEnt('ent_mode').value;
    const showPl = (selVal === 'playlist');
    document.getElementById('frm_playlist').getElementsByClassName("title")[0].style.textDecoration = showPl ? 'none' : 'line-through';
}

function apiGetWifiInfo(){
    fetch("/api/v1/wifi/info").then(async (resp) => {
        const j = await resp.json();
        if(j['stat'] != 'ok'){
            // todo: general error handler!
            return;
        }
        
        console.log(getEnt('ent_wifiMode'));
        getEnt('ent_wifiMode').textContent = j['currentMode'];
        getEnt('ent_wifiSSID').value = j['staSSID'];
    });
}

function apiGetVersion(){
    fetch("/api/v1/version").then(async (resp) => {
        const j = await resp.json();
        if(j['stat'] != 'ok'){
            // todo: general error handler!
            return;
        }
        getEnt('ent_fwVer').textContent = j['version'];
    });
}

function apiGetMode(){
    fetch("/api/v1/mode").then(async (resp) => {
        const j = await resp.json();
        if(j['stat'] != 'ok'){
            // todo: general error handler!
            return;
        }
        console.log(j['mode']);
        getEnt('ent_mode').value = j['mode'];
        changeModeSel();
        getEnt('ent_playlist_mode').value = j['playlist']['mode'];
        getEnt('ent_playlist_dur').value = j['playlist']['duration'];
        
    });
}

function apiGetPmicInfo(){
    fetch("/api/v1/pmic").then(async (resp) => {
        const j = await resp.json();
        if(j['stat'] != 'ok'){
            // todo: general error handler!
            if(pmicRefreshObj != null){
                clearInterval(pmicRefreshObj);
                pmicRefreshObj = null;
            }
            return;
        }
        console.log(j);
        getEnt('ent_pmic_batV').textContent = j['battVolt'].toFixed(2);
        getEnt('ent_pmic_sysV').textContent = j['sysVolt'].toFixed(2);
        getEnt('ent_pmic_usbV').textContent = j['vBusVolt'].toFixed(2);
        getEnt('ent_pmic_batPe').textContent = j['battPercentage'];
        getEnt('ent_pmic_vbusOk').textContent = j['vBusGood'];
        getEnt('ent_pmic_battPres').textContent = j['battPresent'];
        getEnt('ent_pmic_currLim').textContent = j['currLimited'];
    });
}

function apiGetImgList(){
    fetch("/api/v1/img/available").then(async (resp) => {
        const j = await resp.json();
        if(j['stat'] != 'ok'){
            // todo: general error handler!
            return;
        }

        const htmlList = document.getElementById("lst_imgList");
        if(!htmlList){
            console.error(`SD card list ID is invalid`);
            return;
        }
        
        var rows = [];
        // clear the current list, and re-update with what we got
        j['img'].forEach((/** @type {string} */ e) => {
            const div = document.createElement('div');
            const btn = document.createElement('button');
            const span = document.createElement('span');

            btn.textContent = 'X';
            span.textContent = e;
            btn.addEventListener('click', () => clickDeleteImg(e));

            div.appendChild(btn);
            div.appendChild(span);
            rows.push(div);
        });
        htmlList.replaceChildren(...rows);
    });
}

/**
 * Creates a data frame, which contains a label plus some text
 * 
 * @param {string} path
 * @param {object} dat
 * @param {function} after: An optional function to call if the request is successful.
 */
function makePostReqOk(path, dat, after = null){
    const f = fetch(path, {
        method: "POST",
        body: JSON.stringify(dat),
    });

    f.then(async (resp) => {
        const j = await resp.json();
        if(j['stat'] != 'ok'){
            console.error(`Request did not return ok: ${path}`)
            // todo: general error handler!
            return;
        }
        if(after){
            after();
        }
    });
}

function clickSetMode(){
    const selVal = getEnt('ent_mode').value;
    const playlistMode = getEnt('ent_playlist_mode').value;
    const playlistDur = getEnt('ent_playlist_dur').value;
    const playlistDurInt = parseInt(playlistDur);
    if(selVal == ""){
        console.log("Selected value is -, cannot make request");
        return;
    }
    makePostReqOk("/api/v1/mode", {"mode": selVal, "playlist": {"mode": playlistMode, "duration": playlistDurInt}});
}

/**
 * Callback when we clicked to delete an image
 * 
 * @param {string} imgName 
 */
function clickDeleteImg(imgName){
    if(!confirm("Are you sure you want to delete this image?")){
        return;
    }
    console.log(`Deleting image ${imgName}`);
    // make the request to delete the image, and also refresh the existing list after a successful request
    makePostReqOk("/api/v1/img/delete", {name: imgName}, apiGetImgList);
}

/**
 * Callback when we want to add an image to the device
 * This just "clicks" on the input to allow the browser to ask for the image. In part 2 we will actually process the image
 */
function clickAdddImg(){
    document.getElementById('frm_fileInput').click()
}

/**
 * 
 */
async function clickAddImgPart2(){
    const fileInput = /** @type {HTMLInputElement} */ (document.getElementById('frm_fileInput'));
    if (!fileInput.files || fileInput.files.length === 0) return;
    const file = fileInput.files[0];

    const palette = PALETTES['camera'].colors;
    const img = await loadImage(file);
    const scaled = createCanvas(img);
    const dithered = ditherImage(scaled, palette);
    const byteData = ditheredImgToBytes(dithered);
    console.log(byteData);
}