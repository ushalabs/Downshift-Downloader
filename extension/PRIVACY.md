# Downshift extension privacy

Downshift sends download links, filenames, file sizes, selected media details and
download actions to the Downshift desktop application on the same Windows computer.
Media requests can include the page address, referring page, Origin and User-Agent
needed to retrieve the selected media. Download URLs can contain temporary access
tokens supplied by the source website.

Signed-in and generated downloads keep their original browser response. Downshift
receives the location and progress of that response and verifies the final file
before removing its temporary browser copy. Browser cookies, saved passwords and
Authorization headers stay in the browser.

The extension stores its capture preference, transfer receipts and bounded recovery
metadata in the browser's extension storage. Firefox recovery uses hashes and
timestamps to distinguish resumed downloads. Raw source URLs and filenames are
excluded from that Firefox identity cache. The desktop application's download
history remains in its existing local encrypted store.

The extension includes no remote analytics or advertising service. The desktop
application contacts the websites selected for downloads. Downloaded files stay in
the locations chosen in Downshift. Turning off capture in the extension popup lets
the browser handle future downloads.

Firefox classifies the local companion connection as data transmission. Its
installation consent describes browsing activity, website content and download
actions because these are required to send the selected downloads to Downshift.
