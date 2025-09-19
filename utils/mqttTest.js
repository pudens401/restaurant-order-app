// utils/mqttTest.js
const mqttService = require('../services/mqttService');

class MQTTTester {
  // Test publishing a new order (simulate order creation)
  static async testNewOrder() {
    const mockOrder = {
      _id: '507f1f77bcf86cd799439011',
      tableNumber: 5,
      items: [
        {
          menuItem: {
            name: 'Burger Deluxe',
            _id: '507f1f77bcf86cd799439012'
          },
          quantity: 2
        },
        {
          menuItem: {
            name: 'French Fries',
            _id: '507f1f77bcf86cd799439013'
          },
          quantity: 1
        }
      ],
      total: 15000,
      status: 'pending'
    };

    console.log('🧪 Testing MQTT new order publication...');
    const result = await mqttService.publishNewOrder(mockOrder);
    return result;
  }

  // Test order completion (simulate IoT device completing order)
  static testOrderCompletion(orderId = '507f1f77bcf86cd799439011') {
    console.log('🧪 Testing MQTT order completion...');
    
    if (!mqttService.isClientConnected()) {
      console.error('❌ MQTT client not connected');
      return false;
    }

    // Simulate an IoT device sending order completion
    const completionMessage = JSON.stringify({ orderId });
    mqttService.publishTestMessage('KY/RESTO/ORDER/DONE', completionMessage);
    return true;
  }

  // Test with just order ID as string (simpler format)
  static testOrderCompletionSimple(orderId = '507f1f77bcf86cd799439011') {
    console.log('🧪 Testing MQTT order completion (simple format)...');
    
    if (!mqttService.isClientConnected()) {
      console.error('❌ MQTT client not connected');
      return false;
    }

    // Simulate an IoT device sending just the order ID
    mqttService.publishTestMessage('KY/RESTO/ORDER/DONE', orderId);
    return true;
  }

  // Check MQTT connection status
  static getStatus() {
    return {
      connected: mqttService.isClientConnected(),
      topics: {
        newOrder: 'KY/RESTO/ORDER/NEW',
        orderDone: 'KY/RESTO/ORDER/DONE'
      }
    };
  }
}

module.exports = MQTTTester;