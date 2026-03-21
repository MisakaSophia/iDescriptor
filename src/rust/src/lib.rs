use futures::StreamExt;
use idevice::usbmuxd::{Connection, UsbmuxdConnection, UsbmuxdListenEvent};
use std::collections::HashMap;
use std::os::raw::c_char;
use std::{ffi::CString, thread};
use tokio::runtime::Builder;

#[repr(C)]
pub struct IdeviceEvent {
    pub kind: i32, // 1 = connected, 2 = disconnected
    pub udid: *mut c_char,
}

pub type IdeviceEventCallback = extern "C" fn(event: *const IdeviceEvent);

fn make_event(kind: i32, udid: &str) -> IdeviceEvent {
    let c_udid = CString::new(udid).unwrap_or_default();
    IdeviceEvent {
        kind,
        udid: c_udid.into_raw(),
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn idevice_event_subscribe(cb: IdeviceEventCallback) {
    thread::spawn(move || {
        let rt = Builder::new_current_thread().enable_all().build().unwrap();
        rt.block_on(async move {
            let mut device_map: HashMap<u32, String> = HashMap::new();

            loop {
                match UsbmuxdConnection::default().await {
                    Ok(mut uc) => match uc.listen().await {
                        Ok(mut stream) => {
                            while let Some(evt) = stream.next().await {
                                match evt {
                                    Ok(UsbmuxdListenEvent::Connected(d)) => {
                                        // ignore non-USB connections
                                        if d.connection_type != Connection::Usb {
                                            continue;
                                        }

                                        let udid = d.udid.clone();
                                        let device_id = d.device_id;

                                        device_map.insert(device_id, udid.clone());

                                        let ev = make_event(1, &udid);
                                        cb(&ev);
                                    }
                                    Ok(UsbmuxdListenEvent::Disconnected(device_id)) => {
                                        if let Some(udid) = device_map.remove(&device_id) {
                                            let ev = make_event(2, &udid);
                                            cb(&ev);
                                        } else {
                                            eprintln!("Unknown device disconnected: {device_id}");
                                        }
                                    }
                                    Err(e) => {
                                        eprintln!("usbmuxd listen error: {e:?}");
                                        break;
                                    }
                                }
                            }
                        }
                        Err(e) => eprintln!("Failed to start usbmuxd listen: {e:?}"),
                    },
                    // Safe to ignore as it likely means usbmuxd isn't running
                    // usbmuxd is killed when the last device disconnects
                    Err(_) => {}
                }

                tokio::time::sleep(std::time::Duration::from_millis(2000)).await;
            }
        });
    });
}
