// Voron Flow Rate Control Settings Management

$(document).ready(function() {
    // Load Voron settings on page load
    loadVoronSettings();

    // Start status polling
    setInterval(updateVoronStatus, 5000);

    // Save button handler
    $('#btn-voron-save').click(function() {
        saveVoronSettings();
    });

    // Test connection button handler
    $('#btn-voron-test').click(function() {
        testVoronConnection();
    });
});

function loadVoronSettings() {
    $.ajax({
        url: '/api/v0/voron/config',
        type: 'GET',
        dataType: 'json',
        success: function(response) {
            if (response.status === 'ok' && response.data) {
                $('#voron-enabled').prop('checked', response.data.enabled);
                $('#voron-printer-host').val(response.data.printer_host);
                $('#voron-printer-port').val(response.data.printer_port);
                $('#voron-reference-diameter').val(response.data.reference_diameter);
                $('#voron-reference-flow').val(response.data.reference_flow);
                $('#voron-update-threshold').val(response.data.update_threshold);
                $('#voron-min-flow').val(response.data.min_flow);
                $('#voron-max-flow').val(response.data.max_flow);
                $('#voron-update-interval').val(response.data.update_interval_ms);
            }
            updateVoronStatus();
        },
        error: function(xhr, status, error) {
            console.log('Failed to load Voron settings:', error);
            // Voron might not be enabled in this build
            $('#voron-status-badge')
                .removeClass('bg-success bg-danger bg-warning')
                .addClass('bg-secondary')
                .text('Voron not available');
        }
    });
}

function saveVoronSettings() {
    var formData = {
        enabled: $('#voron-enabled').is(':checked') ? 'true' : 'false',
        printer_host: $('#voron-printer-host').val(),
        printer_port: $('#voron-printer-port').val(),
        reference_diameter: $('#voron-reference-diameter').val(),
        reference_flow: $('#voron-reference-flow').val(),
        update_threshold: $('#voron-update-threshold').val(),
        min_flow: $('#voron-min-flow').val(),
        max_flow: $('#voron-max-flow').val(),
        update_interval_ms: $('#voron-update-interval').val()
    };

    $('#btn-voron-save').prop('disabled', true).text('Saving...');

    $.ajax({
        url: '/api/v0/voron/config',
        type: 'POST',
        data: formData,
        dataType: 'json',
        success: function(response) {
            $('#btn-voron-save').prop('disabled', false).text('Save Voron Settings');
            if (response.status === 'ok') {
                alert('Voron settings saved successfully!');
                // Update status after a short delay
                setTimeout(updateVoronStatus, 1000);
            } else {
                alert('Failed to save: ' + (response.message || 'Unknown error'));
            }
        },
        error: function(xhr, status, error) {
            $('#btn-voron-save').prop('disabled', false).text('Save Voron Settings');
            alert('Failed to save Voron settings: ' + error);
        }
    });
}

function testVoronConnection() {
    $('#btn-voron-test').prop('disabled', true).text('Testing...');

    $.ajax({
        url: '/api/v0/voron/test',
        type: 'POST',
        dataType: 'json',
        success: function(response) {
            $('#btn-voron-test').prop('disabled', false).text('Test Connection');
            if (response.status === 'ok') {
                alert('Connection successful! Printer is reachable.');
            } else {
                alert('Connection failed: ' + (response.message || 'Unknown error'));
            }
            updateVoronStatus();
        },
        error: function(xhr, status, error) {
            $('#btn-voron-test').prop('disabled', false).text('Test Connection');
            alert('Connection test failed: ' + error);
            updateVoronStatus();
        }
    });
}

function updateVoronStatus() {
    $.ajax({
        url: '/api/v0/voron/status',
        type: 'GET',
        dataType: 'json',
        success: function(response) {
            if (response.status === 'ok' && response.data) {
                var badge = $('#voron-status-badge');
                var flowDisplay = $('#voron-flow-display');
                badge.removeClass('bg-success bg-danger bg-warning bg-secondary');

                if (!response.data.enabled) {
                    badge.addClass('bg-secondary').text('Status: Disabled');
                    flowDisplay.text('');
                } else if (response.data.last_error) {
                    badge.addClass('bg-danger').text('Status: Error');
                    if (response.data.last_flow > 0) {
                        flowDisplay.text('Last flow: ' + response.data.last_flow.toFixed(1) + '%');
                    } else {
                        flowDisplay.text('');
                    }
                } else if (response.data.last_flow > 0) {
                    badge.addClass('bg-success').text('Status: Active');
                    flowDisplay.text('Flow: ' + response.data.last_flow.toFixed(1) + '% (d=' + response.data.last_diameter.toFixed(2) + 'mm)');
                } else {
                    badge.addClass('bg-warning').text('Status: Waiting');
                    flowDisplay.text('');
                }
            }
        },
        error: function(xhr, status, error) {
            // Silently fail status updates
        }
    });
}
