// test-message-size.js - Test MQTT message sizes for different order configurations
require('dotenv').config();

// Mock order data to test message sizes
const testOrders = [
  {
    // Single item order
    name: "Single Item Order",
    order: {
      _id: "675c1a2b3d4e5f6789abcdef",
      tableNumber: 5,
      items: [
        {
          menuItem: { name: "Burger" },
          quantity: 1
        }
      ],
      total: 5000
    }
  },
  {
    // Multi-item order (3 items)
    name: "3-Item Order",
    order: {
      _id: "675c1a2b3d4e5f6789abcdef",
      tableNumber: 12,
      items: [
        {
          menuItem: { name: "Grilled Chicken with Rice" },
          quantity: 2
        },
        {
          menuItem: { name: "Beef Burger with Fries" },
          quantity: 1
        },
        {
          menuItem: { name: "Chocolate Ice Cream" },
          quantity: 3
        }
      ],
      total: 18000
    }
  },
  {
    // Large order (6 items with long names)
    name: "6-Item Order with Long Names",
    order: {
      _id: "675c1a2b3d4e5f6789abcdef",
      tableNumber: 8,
      items: [
        {
          menuItem: { name: "Grilled Chicken Breast with Steamed Rice and Mixed Vegetables" },
          quantity: 2
        },
        {
          menuItem: { name: "Premium Beef Burger with Crispy French Fries and Garden Salad" },
          quantity: 1
        },
        {
          menuItem: { name: "Fresh Atlantic Salmon with Lemon Butter and Asparagus" },
          quantity: 1
        },
        {
          menuItem: { name: "Chocolate Fudge Brownie with Vanilla Ice Cream and Whipped Cream" },
          quantity: 2
        },
        {
          menuItem: { name: "Caesar Salad with Grilled Chicken and Parmesan Cheese" },
          quantity: 1
        },
        {
          menuItem: { name: "Freshly Squeezed Orange Juice with Pulp" },
          quantity: 4
        }
      ],
      total: 45000
    }
  }
];

function createOrderData(order) {
  return {
    orderId: order._id.toString(),
    tableNumber: order.tableNumber,
    items: order.items.map(item => ({
      name: item.menuItem.name,
      quantity: item.quantity,
      notes: item.notes || ''
    })),
    timestamp: new Date().toISOString(),
    total: order.total
  };
}

function createOptimizedOrderData(order) {
  return {
    orderId: order._id.toString(),
    tableNumber: order.tableNumber,
    items: order.items.map(item => ({
      name: item.menuItem.name.length > 30 ? 
            item.menuItem.name.substring(0, 30) + '...' : 
            item.menuItem.name,
      quantity: item.quantity,
      notes: item.notes || ''
    })),
    timestamp: new Date().toISOString(),
    total: order.total
  };
}

console.log('🧪 Testing MQTT Message Sizes for Different Order Configurations\\n');

testOrders.forEach(test => {
  console.log(`📋 ${test.name}:`);
  
  // Original format (pretty printed)
  const prettyMessage = JSON.stringify(createOrderData(test.order), null, 2);
  console.log(`   Pretty JSON: ${prettyMessage.length} bytes`);
  
  // Compact format
  const compactMessage = JSON.stringify(createOrderData(test.order));
  console.log(`   Compact JSON: ${compactMessage.length} bytes`);
  
  // Optimized format (truncated names)
  const optimizedMessage = JSON.stringify(createOptimizedOrderData(test.order));
  console.log(`   Optimized JSON: ${optimizedMessage.length} bytes`);
  
  // Show status
  if (compactMessage.length > 4096) {
    console.log(`   ❌ TOO LARGE for Arduino buffer (4096 bytes)`);
  } else if (compactMessage.length > 2048) {
    console.log(`   ⚠️  Would fail with old buffer size (2048 bytes)`);
  } else {
    console.log(`   ✅ Fits in buffer`);
  }
  
  console.log();
});

console.log('💡 Recommendations:');
console.log('   • Use compact JSON (no pretty printing)');
console.log('   • Arduino buffer should be at least 4096 bytes');
console.log('   • Consider truncating very long item names');
console.log('   • Monitor message sizes in production');