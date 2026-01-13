// MQTT Settings Management

$(document).ready(function() {
    // Load MQTT settings on page load
    loadMqttSettings();

    // Start status polling
    setInterval(updateMqttStatus, 5000);

    // Save button handler
    $('#btn-mqtt-save').click(function() {
        saveMqttSettings();
    });
});

function loadMqttSettings() {
    $.ajax({
        url: '/api/v0/mqtt/config',
        type: 'GET',
        dataType: 'json',
        success: function(response) {
            if (response.status === 'ok' && response.data) {
                $('#mqtt-enabled').prop('checked', response.data.enabled);
                $('#mqtt-broker-host').val(response.data.broker_host);
                $('#mqtt-broker-port').val(response.data.broker_port);
                $('#mqtt-username').val(response.data.username);
                $('#mqtt-device-id').val(response.data.device_id);
                $('#mqtt-publish-threshold').val(response.data.publish_threshold);
                // Password is not returned for security reasons
            }
            updateMqttStatus();
        },
        error: function(xhr, status, error) {
            console.log('Failed to load MQTT settings:', error);
            // MQTT might not be enabled in this build
            $('#mqtt-status-badge')
                .removeClass('bg-success bg-danger bg-warning')
                .addClass('bg-secondary')
                .text('MQTT not available');
        }
    });
}

function saveMqttSettings() {
    var formData = {
        enabled: $('#mqtt-enabled').is(':checked') ? 'true' : 'false',
        broker_host: $('#mqtt-broker-host').val(),
        broker_port: $('#mqtt-broker-port').val(),
        username: $('#mqtt-username').val(),
        password: $('#mqtt-password').val(),
        device_id: $('#mqtt-device-id').val(),
        publish_threshold: $('#mqtt-publish-threshold').val()
    };

    $('#btn-mqtt-save').prop('disabled', true).text('Saving...');

    $.ajax({
        url: '/api/v0/mqtt/config',
        type: 'POST',
        data: formData,
        dataType: 'json',
        success: function(response) {
            $('#btn-mqtt-save').prop('disabled', false).text('Save MQTT Settings');
            if (response.status === 'ok') {
                alert('MQTT settings saved successfully!');
                // Clear password field after save
                $('#mqtt-password').val('');
                // Update status after a short delay to allow reconnection
                setTimeout(updateMqttStatus, 2000);
            } else {
                alert('Failed to save: ' + (response.message || 'Unknown error'));
            }
        },
        error: function(xhr, status, error) {
            $('#btn-mqtt-save').prop('disabled', false).text('Save MQTT Settings');
            alert('Failed to save MQTT settings: ' + error);
        }
    });
}

function updateMqttStatus() {
    $.ajax({
        url: '/api/v0/mqtt/status',
        type: 'GET',
        dataType: 'json',
        success: function(response) {
            if (response.status === 'ok' && response.data) {
                var badge = $('#mqtt-status-badge');
                badge.removeClass('bg-success bg-danger bg-warning bg-secondary');

                if (!response.data.enabled) {
                    badge.addClass('bg-secondary').text('Status: Disabled');
                } else if (response.data.connected) {
                    badge.addClass('bg-success').text('Status: Connected');
                } else {
                    badge.addClass('bg-warning').text('Status: Disconnected');
                }
            }
        },
        error: function(xhr, status, error) {
            // Silently fail status updates
        }
    });
}
